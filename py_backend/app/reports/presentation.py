from __future__ import annotations

import io
import json
import math
import re
import statistics
import subprocess
from copy import deepcopy
from functools import lru_cache
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple, Union

from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
from matplotlib import patheffects
import cairosvg
from PIL import Image, ImageDraw, ImageFont
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.oxml.ns import qn
from pptx.oxml.xmlchemy import OxmlElement
from pptx.util import Inches, Pt


PROJECT_DIR = Path(__file__).resolve().parents[2]
ASSET_DIR = PROJECT_DIR / "assets"
MANIFEST_PATH = ASSET_DIR / "ebsd_report_template.json"
FRONTEND_CHART_RENDERER = (
    PROJECT_DIR.parent / "frontend" / "tools" / "render_grain_charts.mjs"
)
FONT_FACE = "Arial"
INK = RGBColor(0, 0, 0)
MUTED = RGBColor(89, 89, 89)
ORIENTATION_SAMPLE = "#2563EB"
ORIENTATION_GOLDEN = "#F59E0B"
OVERRIDDEN_TEXT = RGBColor(0, 0, 255)
POSITION_RE = re.compile(r"^([A-Z]+)-([1-9]\d*)$")

Snapshot = Dict[str, Dict[str, Any]]
ReportData = Dict[str, Snapshot]


def _excel_number(column: str) -> int:
    result = 0
    for char in column:
        result = result * 26 + ord(char) - 64
    return result


def _excel_column(number: int) -> str:
    result = ""
    while number > 0:
        number -= 1
        result = chr(65 + number % 26) + result
        number //= 26
    return result


def _position_key(value: str) -> Tuple[int, int, str]:
    match = POSITION_RE.fullmatch(str(value).strip().upper())
    if not match:
        return (10**9, 10**9, str(value))
    return (int(match.group(2)), _excel_number(match.group(1)), str(value))


def _scan_position_key(value: str) -> Tuple[int, int, str]:
    match = POSITION_RE.fullmatch(str(value).strip().upper())
    if not match:
        return (10**9, 10**9, str(value))
    return (_excel_number(match.group(1)), int(match.group(2)), str(value))


def _positions(*snapshots: Snapshot) -> List[str]:
    return sorted({pos for snapshot in snapshots for pos in snapshot}, key=_position_key)


def _matrix_positions(positions: Iterable[str]) -> Tuple[List[str], List[int]]:
    parsed = [POSITION_RE.fullmatch(str(pos).strip().upper()) for pos in positions]
    parsed = [match for match in parsed if match]
    if not parsed:
        return [], []
    max_column = max(_excel_number(match.group(1)) for match in parsed)
    max_row = max(int(match.group(2)) for match in parsed)
    return [_excel_column(value) for value in range(1, max_column + 1)], list(range(1, max_row + 1))


def _nice_percent_max(values: Iterable[float], *, floor: float = 10.0) -> float:
    finite = [float(value) for value in values if math.isfinite(float(value))]
    maximum = max(finite, default=0.0)
    return max(floor, math.ceil(maximum / 10.0) * 10.0)


def _triangle_percent_max(values: Iterable[float]) -> float:
    """Use the product rule for the shared triangle scale."""
    finite = [float(value) for value in values if math.isfinite(float(value))]
    maximum = max(finite, default=0.0)
    return 50.0 if maximum <= 50.0 else math.ceil(maximum / 10.0) * 10.0


def _label_stride(item_count: int, visible_capacity: int) -> int:
    """Return a deterministic sampling stride for dense one-slide labels."""
    return max(1, math.ceil(max(0, item_count) / max(1, visible_capacity)))


def _sampled_label_indices(item_count: int, stride: int) -> List[int]:
    """Sample labels evenly and keep the last label without crowding it."""
    if item_count <= 0:
        return []
    stride = max(1, int(stride))
    indices = list(range(0, item_count, stride))
    last = item_count - 1
    if indices[-1] != last:
        if last - indices[-1] < stride:
            indices[-1] = last
        else:
            indices.append(last)
    return indices


def _triangle_annotation_mode(density_scale: float, show_values: bool) -> Tuple[bool, bool]:
    """Return axis/value visibility without ever drawing unreadably tiny text."""
    scale = float(density_scale)
    axis_readable = scale >= 0.28
    values_readable = scale >= 0.50
    return axis_readable, bool(show_values) and values_readable


def _finite(values: Any) -> List[float]:
    if not isinstance(values, list):
        return []
    return [float(value) for value in values if isinstance(value, (int, float)) and math.isfinite(float(value))]


def _orientation_labels(snapshot: Snapshot) -> List[str]:
    for item in snapshot.values():
        labels = item.get("orientation_labels")
        if isinstance(labels, list) and labels:
            return [str(value) for value in labels[:3]]
    return ["[001]", "[011]", "[111]"]


def _orientation_values(item: Optional[Dict[str, Any]], deviation: str, *, grains: bool = False) -> List[float]:
    prefix = "orientation_ratio_by_grains" if grains else "orientation_ratio"
    values = item.get(f"{prefix}({deviation})") if item else None
    if not isinstance(values, list):
        return [0.0, 0.0, 0.0]
    return [float(values[index] if index < len(values) else 0.0) * 100 for index in range(3)]


class TemplateDeck:
    def __init__(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        if manifest.get("schema_version") != 2:
            raise RuntimeError("unsupported EBSD PPT template manifest")
        self.prs = Presentation(ASSET_DIR / manifest["template_file"])
        self.manifest = manifest
        self.prototype_count = len(self.prs.slides)
        self.prototypes: Dict[str, Any] = {}
        for kind, spec in manifest["prototypes"].items():
            slide = self.prs.slides[int(spec["slide_number"]) - 1]
            if not any(shape.name == spec["prototype_shape"] for shape in slide.shapes):
                raise RuntimeError(f"invalid clean template prototype: {kind}")
            self.prototypes[kind] = slide

    def add_slide(self, title: str, subtitle: str = "", kind: str = "generic"):
        prototype = self.prototypes.get(kind, self.prototypes["generic"])
        slide = self.prs.slides.add_slide(prototype.slide_layout)
        for shape in prototype.shapes:
            slide.shapes._spTree.insert_element_before(deepcopy(shape.element), "p:extLst")
        title_shape = self.shape(slide, "EBSD_TITLE")
        _replace_shape_text(title_shape, title)
        heading = self.manifest["page_heading"]
        title_shape.left = Inches(heading["title_left_in"])
        title_shape.top = Inches(heading["title_top_in"])
        title_shape.width = Inches(heading["title_width_in"])
        title_shape.height = Inches(heading["title_height_in"])
        subtitle_shape = next((shape for shape in slide.shapes if shape.name == "EBSD_SUBTITLE"), None)
        if subtitle_shape is not None:
            _replace_shape_text(subtitle_shape, subtitle)
            subtitle_shape.left = Inches(heading["subtitle_left_in"])
            subtitle_shape.top = Inches(heading["subtitle_top_in"])
            subtitle_shape.width = Inches(heading["subtitle_width_in"])
            subtitle_shape.height = Inches(heading["subtitle_height_in"])
        elif subtitle:
            subtitle_shape = _add_text(
                slide,
                subtitle,
                Inches(heading["subtitle_left_in"]),
                Inches(heading["subtitle_top_in"]),
                Inches(heading["subtitle_width_in"]),
                Inches(heading["subtitle_height_in"]),
                size=10,
                color=MUTED,
            )
            subtitle_shape.name = "EBSD_SUBTITLE"
        return slide

    @staticmethod
    def shape(slide, name: str):
        shape = next((candidate for candidate in slide.shapes if candidate.name == name), None)
        if shape is None:
            raise RuntimeError(f"template shape is missing: {name}")
        return shape

    def remove_shape(self, slide, name: str) -> None:
        shape = next((candidate for candidate in slide.shapes if candidate.name == name), None)
        if shape is not None:
            shape._element.getparent().remove(shape._element)

    def apply_table_template(self, table, kind: str, name: str) -> None:
        prototype = self.shape(self.prototypes[kind], name).table
        current = table._tbl.tblPr
        current.getparent().replace(current, deepcopy(prototype._tbl.tblPr))

    @property
    def content(self) -> Tuple[int, int, int, int]:
        box = self.manifest["generic_content_box"]
        return tuple(Inches(box[key]) for key in ("left_in", "top_in", "width_in", "height_in"))  # type: ignore[return-value]

    def save(self) -> bytes:
        for _ in range(self.prototype_count):
            slide_id = self.prs.slides._sldIdLst[0]
            self.prs.part.drop_rel(slide_id.rId)
            del self.prs.slides._sldIdLst[0]
        output = io.BytesIO()
        self.prs.save(output)
        return output.getvalue()


def _replace_shape_text(shape, text: str) -> None:
    if not shape.has_text_frame:
        return
    runs = [run for paragraph in shape.text_frame.paragraphs for run in paragraph.runs]
    if runs:
        runs[0].text = str(text)
        for run in runs[1:]:
            run.text = ""
        return
    shape.text_frame.text = str(text)


def _set_scan_title(deck: TemplateDeck, slide, sample: str, position: str, reanalysis: bool) -> None:
    shape = deck.shape(slide, "EBSD_TITLE")
    frame = shape.text_frame
    frame.clear()
    paragraph = frame.paragraphs[0]
    base = paragraph.add_run()
    base.text = f"{sample} {position}"
    base.font.name = FONT_FACE
    base.font.size = Pt(24)
    base.font.bold = True
    base.font.color.rgb = INK
    if reanalysis:
        suffix = paragraph.add_run()
        suffix.text = " (Reanalysis)"
        suffix.font.name = FONT_FACE
        suffix.font.size = Pt(24)
        suffix.font.bold = True
        suffix.font.color.rgb = RGBColor(0, 0, 255)


def _add_text(slide, text: str, left: int, top: int, width: int, height: int, *, size: int = 10, bold: bool = False, color: RGBColor = INK, align=PP_ALIGN.LEFT):
    shape = slide.shapes.add_textbox(left, top, width, height)
    frame = shape.text_frame
    frame.clear()
    frame.word_wrap = True
    frame.vertical_anchor = MSO_ANCHOR.MIDDLE
    paragraph = frame.paragraphs[0]
    paragraph.alignment = align
    run = paragraph.add_run()
    run.text = str(text)
    run.font.name = FONT_FACE
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = color
    return shape


def _format_measurement(value: float) -> str:
    return f"{value:.3f}".rstrip("0").rstrip(".")


def _histogram_width_note(edges: Sequence[float]) -> str:
    width = edges[1] - edges[0] if len(edges) >= 2 else 0.0
    return f"Bar width: {_format_measurement(width)} µm"


def _golden_data_note(golden_sample: str, golden_version_label: str) -> str:
    sample = str(golden_sample or "").strip()
    version = str(golden_version_label or "").strip()
    parts = [sample] if sample else []
    if version and version != sample:
        parts.append(version)
    identity = " · ".join(parts)
    return f"Golden data: {identity}" if identity else ""


def _add_report_note(slide, text: str) -> None:
    if not text:
        return
    subtitle = next((candidate for candidate in slide.shapes if candidate.name == "EBSD_SUBTITLE"), None)
    if subtitle is not None:
        subtitle.width = min(subtitle.width, Inches(7.9))
    shape = _add_text(
        slide,
        text,
        Inches(8.15),
        Inches(0.57),
        Inches(5.0),
        Inches(0.26),
        size=9,
        color=MUTED,
        align=PP_ALIGN.RIGHT,
    )
    shape.name = "EBSD_REPORT_NOTE"


def _set_cell(cell, text: str, *, header: bool = False, size: int = 8, color: Optional[RGBColor] = None) -> None:
    cell.margin_left = Inches(0.04)
    cell.margin_right = Inches(0.04)
    cell.margin_top = Inches(0.02)
    cell.margin_bottom = Inches(0.02)
    cell.text = ""
    cell.vertical_anchor = MSO_ANCHOR.MIDDLE
    paragraph = cell.text_frame.paragraphs[0]
    paragraph.alignment = PP_ALIGN.CENTER
    run = paragraph.add_run()
    run.text = str(text)
    run.font.name = FONT_FACE
    run.font.size = Pt(size)
    run.font.bold = header
    run.font.color.rgb = color or INK


def _scale_table_margins(table, scale: float) -> None:
    """Keep padding proportional to the responsive cell size."""
    scale = max(0.10, min(1.0, float(scale)))
    for row in table.rows:
        for cell in row.cells:
            cell.margin_left = Inches(0.04 * scale)
            cell.margin_right = Inches(0.04 * scale)
            cell.margin_top = Inches(0.02 * scale)
            cell.margin_bottom = Inches(0.02 * scale)


def _style_table_grid(table, colour: str = "B8BEC7", width: str = "6350") -> None:
    """Use quiet half-point rules so dense tables do not turn into barcodes."""
    for row in table.rows:
        for cell in row.cells:
            properties = cell._tc.get_or_add_tcPr()
            for edge in ("lnL", "lnR", "lnT", "lnB"):
                tag = qn(f"a:{edge}")
                for existing in list(properties.findall(tag)):
                    properties.remove(existing)
                line = OxmlElement(f"a:{edge}")
                line.set("w", width)
                fill = OxmlElement("a:solidFill")
                rgb = OxmlElement("a:srgbClr")
                rgb.set("val", colour)
                fill.append(rgb)
                line.append(fill)
                dash = OxmlElement("a:prstDash")
                dash.set("val", "solid")
                line.append(dash)
                properties.append(line)


def _add_native_matrix(
    deck: TemplateDeck,
    slide,
    positions: Sequence[str],
    value_for_position,
    *,
    header_column_width: float = 0.992,
    data_column_width: float = 0.992,
    row_height: float = 0.406,
    style_kind: str = "orientation_pixels",
    style_shape: str = "EBSD_TABLE_20",
    font_size: int = 18,
) -> None:
    columns, rows = _matrix_positions(positions)
    if not columns or not rows:
        left, top, width, height = deck.content
        _add_text(slide, "沒有可輸出的動態位置資料", left, top, width, height, size=16, color=MUTED, align=PP_ALIGN.CENTER)
        return
    left, top, _, _ = deck.content
    table_width = Inches(header_column_width + len(columns) * data_column_width)
    table_height = Inches((len(rows) + 1) * row_height)
    table = slide.shapes.add_table(len(rows) + 1, len(columns) + 1, left, top, table_width, table_height).table
    deck.apply_table_template(table, style_kind, style_shape)
    table.columns[0].width = Inches(header_column_width)
    for index in range(1, len(columns) + 1):
        table.columns[index].width = Inches(data_column_width)
    for row in table.rows:
        row.height = Inches(row_height)
    _set_cell(table.cell(0, 0), "位置", header=True, size=font_size)
    for column_index, column in enumerate(columns, start=1):
        _set_cell(table.cell(0, column_index), column, header=True, size=font_size)
    position_set = set(positions)
    for row_index, row in enumerate(rows, start=1):
        _set_cell(table.cell(row_index, 0), str(row), header=True, size=font_size)
        for column_index, column in enumerate(columns, start=1):
            position = f"{column}-{row}"
            _set_cell(table.cell(row_index, column_index), value_for_position(position) if position in position_set else "", size=font_size)


def _add_picture_contain(slide, data: Union[bytes, io.BytesIO], left: int, top: int, width: int, height: int):
    stream = data if isinstance(data, io.BytesIO) else io.BytesIO(data)
    stream.seek(0)
    with Image.open(stream) as image:
        image_width, image_height = image.size
    stream.seek(0)
    ratio = min(width / max(image_width, 1), height / max(image_height, 1))
    draw_width = int(image_width * ratio)
    draw_height = int(image_height * ratio)
    return slide.shapes.add_picture(
        stream,
        left + (width - draw_width) // 2,
        top + (height - draw_height) // 2,
        draw_width,
        draw_height,
    )


def _add_captioned_picture(
    slide,
    caption: str,
    image: Optional[bytes],
    left: float,
    top: float,
    width: float,
    height: float,
    *,
    picture_name: Optional[str] = None,
):
    _add_text(
        slide,
        caption,
        Inches(left),
        Inches(top),
        Inches(width),
        Inches(0.28),
        size=12,
        bold=True,
        align=PP_ALIGN.CENTER,
    )
    if image:
        picture = _add_picture_contain(
            slide,
            image,
            Inches(left),
            Inches(top + 0.30),
            Inches(width),
            Inches(height - 0.30),
        )
        if picture_name:
            picture.name = picture_name
        return picture
    return None


def _legend_font(size: int, *, bold: bool = False):
    candidates = (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    )
    for path in candidates:
        try:
            return ImageFont.truetype(path, size=size)
        except OSError:
            continue
    return ImageFont.load_default()


@lru_cache(maxsize=16)
def _deviation_legend(labels: Tuple[str, str, str], deviation: int) -> bytes:
    """Render solid angular-tolerance regions in the cubic IPF sector."""
    scale = 3
    width, height = 380 * scale, 500 * scale
    image = Image.new("RGB", (width, height), "white")
    pixels = image.load()
    origin = (48 * scale, 50 * scale)
    projection_scale = 570 * scale
    right = math.sqrt((1.0 - 1.0 / math.sqrt(3.0)) / 2.0)
    down = math.sqrt(1.0 - 1.0 / math.sqrt(2.0))
    bottom_left = (origin[0], round(origin[1] + down * projection_scale))
    right_vertex = (
        round(origin[0] + right * projection_scale),
        round(origin[1] + right * projection_scale),
    )
    colours = ((242, 92, 92), (89, 209, 99), (102, 115, 230))
    inverse_sqrt_two = 1.0 / math.sqrt(2.0)
    inverse_sqrt_three = 1.0 / math.sqrt(3.0)
    targets = (
        (0.0, 0.0, 1.0),
        (inverse_sqrt_two, 0.0, inverse_sqrt_two),
        (inverse_sqrt_three, inverse_sqrt_three, inverse_sqrt_three),
    )

    # This is the same normalized Lambert equal-area projection and cubic
    # fundamental-sector test used by the inverse-pole-figure plot below.
    # Colour is binary: a point belongs to its nearest family only when its
    # crystallographic angular distance is within the requested deviation.
    for pixel_y in range(origin[1], bottom_left[1] + 1):
        plot_y = -(pixel_y - origin[1]) / projection_scale
        projected_x = -plot_y
        for pixel_x in range(origin[0], right_vertex[0] + 1):
            plot_x = (pixel_x - origin[0]) / projection_scale
            projected_y = plot_x
            radius_squared = projected_x * projected_x + projected_y * projected_y
            if radius_squared > 1.0:
                continue
            crystal_z = 1.0 - radius_squared
            radius = math.sqrt(radius_squared)
            if radius <= 1e-12:
                crystal_x, crystal_y = 0.0, 0.0
            else:
                equatorial = math.sqrt(max(1.0 - crystal_z * crystal_z, 0.0))
                crystal_x = projected_x * equatorial / radius
                crystal_y = projected_y * equatorial / radius
            if crystal_y < 0.0 or crystal_x < crystal_y or crystal_z < crystal_x:
                continue

            angles = [
                math.degrees(
                    math.acos(
                        max(
                            -1.0,
                            min(
                                1.0,
                                crystal_x * target[0]
                                + crystal_y * target[1]
                                + crystal_z * target[2],
                            ),
                        )
                    )
                )
                for target in targets
            ]
            nearest = min(range(3), key=angles.__getitem__)
            if angles[nearest] <= deviation:
                # Keep the orientation families unambiguous while replacing
                # the old flat colour blocks with a smooth confidence ramp.
                # The family colour is strongest at the crystallographic
                # endpoint and fades toward the tolerance boundary.
                strength = max(0.0, min(1.0, 1.0 - angles[nearest] / max(deviation, 1)))
                blend = 0.22 + 0.78 * strength
                colour = colours[nearest]
                pixels[pixel_x, pixel_y] = tuple(
                    round(255.0 * (1.0 - blend) + channel * blend)
                    for channel in colour
                )

    draw = ImageDraw.Draw(image)
    line_width = 4 * scale
    draw.line((origin, bottom_left), fill="black", width=line_width)
    draw.line((origin, right_vertex), fill="black", width=line_width)
    curved_boundary = []
    for step in range(101):
        parameter = step / 100.0
        norm = math.sqrt(2.0 + parameter * parameter)
        crystal_x = 1.0 / norm
        crystal_y = parameter / norm
        crystal_z = 1.0 / norm
        boundary_radius = math.sqrt(1.0 - crystal_z)
        boundary_equatorial = math.hypot(crystal_x, crystal_y)
        plot_x = boundary_radius * crystal_y / boundary_equatorial
        plot_y = -boundary_radius * crystal_x / boundary_equatorial
        curved_boundary.append(
            (
                round(origin[0] + plot_x * projection_scale),
                round(origin[1] - plot_y * projection_scale),
            )
        )
    draw.line(curved_boundary, fill="black", width=line_width, joint="curve")

    label_font = _legend_font(31 * scale, bold=True)
    deviation_font = _legend_font(28 * scale, bold=True)
    cleaned = tuple(str(label).strip().strip("[]<>") for label in labels)
    draw.text((origin[0], origin[1] - 15 * scale), cleaned[0], fill="black", font=label_font, anchor="ms")
    draw.text((bottom_left[0], bottom_left[1] + 13 * scale), cleaned[1], fill="black", font=label_font, anchor="lt")
    draw.text((right_vertex[0] + 10 * scale, right_vertex[1]), cleaned[2], fill="black", font=label_font, anchor="lm")
    draw.text(
        (width // 2, 438 * scale),
        f"Deviation: {deviation}\N{DEGREE SIGN}",
        fill="black",
        font=deviation_font,
        anchor="mm",
    )

    output = io.BytesIO()
    image.resize((380, 500), Image.Resampling.LANCZOS).save(output, format="PNG")
    return output.getvalue()


def _add_grain_summary_table(slide, item: Dict[str, Any], left: float, top: float, width: float, height: float) -> None:
    grains = _finite(item.get("grains"))
    values = (
        ("Average", f"{statistics.mean(grains):.3f}" if grains else "—"),
        ("Max", f"{max(grains):.3f}" if grains else "—"),
        ("Quantity", str(len(grains))),
    )
    table = slide.shapes.add_table(4, 2, Inches(left), Inches(top), Inches(width), Inches(height)).table
    table.columns[0].width = Inches(width * 0.57)
    table.columns[1].width = Inches(width * 0.43)
    _set_cell(table.cell(0, 0), "Grain size", header=True, size=11)
    _set_cell(table.cell(0, 1), "(µm)", header=True, size=11)
    for row_index, (label, value) in enumerate(values, start=1):
        _set_cell(table.cell(row_index, 0), label, size=10)
        _set_cell(table.cell(row_index, 1), value, size=10)


def _add_ratio_table(slide, item: Dict[str, Any], deviation: str, left: float, top: float, width: float, height: float) -> None:
    labels = _orientation_labels({"_": item})
    values = _orientation_values(item, deviation)
    table = slide.shapes.add_table(2, 3, Inches(left), Inches(top), Inches(width), Inches(height)).table
    for column in table.columns:
        column.width = Inches(width / 3)
    _style_table_grid(table, colour="FFFFFF", width="12700")
    for column_index in range(3):
        header_cell = table.cell(0, column_index)
        _set_cell(
            header_cell,
            f"<{labels[column_index].strip('[]< >')}>",
            header=True,
            size=13,
            color=RGBColor(255, 255, 255),
        )
        header_cell.fill.solid()
        header_cell.fill.fore_color.rgb = RGBColor(70, 167, 193)
        value_cell = table.cell(1, column_index)
        _set_cell(value_cell, f"{values[column_index]:.1f}%", size=12)
        value_cell.fill.solid()
        value_cell.fill.fore_color.rgb = RGBColor(235, 247, 251)


def _frontend_charts(
    charts: Sequence[Dict[str, Any]],
    *,
    shared_x_max: bool = False,
    shared_y_max: bool = False,
    result_metadata: Optional[Dict[str, Any]] = None,
) -> List[bytes]:
    """Render PPT charts with the exact JavaScript renderer used by Vue."""
    if not FRONTEND_CHART_RENDERER.is_file():
        raise RuntimeError(f"Frontend chart renderer not found: {FRONTEND_CHART_RENDERER}")
    request = json.dumps(
        {
            "charts": list(charts),
            "sharedXMax": shared_x_max,
            "sharedYMax": shared_y_max,
        },
        ensure_ascii=False,
    )
    try:
        process = subprocess.run(
            ["node", str(FRONTEND_CHART_RENDERER)],
            input=request,
            text=True,
            capture_output=True,
            check=True,
            timeout=20,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        detail = getattr(exc, "stderr", "") or str(exc)
        raise RuntimeError(f"V8 frontend chart rendering failed: {detail}") from exc
    response = json.loads(process.stdout)
    if result_metadata is not None:
        result_metadata.update(response)
    rendered = response.get("charts", [])
    if len(rendered) != len(charts):
        raise RuntimeError("V8 frontend chart renderer returned an unexpected chart count")
    return [
        cairosvg.svg2png(bytestring=item["svg"].encode("utf-8"), output_width=1000)
        for item in rendered
    ]


def _frontend_distribution_charts(charts: Sequence[Dict[str, Any]]) -> List[bytes]:
    return _frontend_charts(charts, shared_x_max=True, shared_y_max=True)


def _add_microstructure_page(deck: TemplateDeck, sample: str, position: str, images: Dict[str, Any], reanalysis: bool) -> None:
    if not any(images.get(name) for name in ("grain_boundaries", "csl_map", "kam_map", "gos_map")):
        return
    slide = deck.add_slide("", kind="generic")
    _set_scan_title(deck, slide, sample, position, reanalysis)
    kam_chart, gos_chart = _frontend_distribution_charts(
        [
            {
                "sample": images.get("kam_values", []),
                "golden": [],
                "mode": "hist",
                "histYMode": "count",
                "fixedXMin": 0,
                "autoBinCount": True,
                "showXAxis": True,
                "showYAxis": True,
                "xAxisLabel": "KAM (°)",
                "sampleColor": "#0ea5e9",
                "sampleDash": "",
                "showAverage": True,
                "averageSuffix": "°",
            },
            {
                "sample": images.get("gos_values", []),
                "golden": [],
                "mode": "hist",
                "histYMode": "count",
                "fixedXMin": 0,
                "autoBinCount": True,
                "showXAxis": True,
                "showYAxis": True,
                "xAxisLabel": "GOS (°)",
                "sampleColor": "#10b981",
                "sampleDash": "",
                "showAverage": True,
                "averageSuffix": "°",
            },
        ]
    )
    slots = (
        ("Grain boundaries", images.get("grain_boundaries"), 0.35, 0.72),
        ("Coincident site lattice (CSL)", images.get("csl_map"), 4.55, 0.72),
        ("Kernel average misorientation (KAM)", images.get("kam_map"), 8.75, 0.72),
        ("Grain orientation spread (GOS)", images.get("gos_map"), 0.35, 3.92),
        ("KAM distribution", kam_chart, 4.55, 3.92),
        ("GOS distribution", gos_chart, 8.75, 3.92),
    )
    for caption, image, left, top in slots:
        _add_captioned_picture(slide, caption, image, left, top, 3.95, 2.72)
    summary = images.get("csl_summary")
    if isinstance(summary, dict) and summary:
        text = "  ".join(f"{name}: {float(value):.1f}%" for name, value in summary.items())
        _add_text(slide, text, Inches(4.68), Inches(3.43), Inches(3.70), Inches(0.28), size=10, align=PP_ALIGN.CENTER)


def _add_scan_point_pages(
    deck: TemplateDeck,
    sample: str,
    snapshot: Snapshot,
    images_by_position: Dict[str, Dict[str, Any]],
    overridden_positions: Sequence[str],
    ipf_legend: Optional[bytes],
    histogram_settings: Dict[str, Any],
    *,
    show_microstructure_page: bool = False,
) -> None:
    overridden = set(overridden_positions)
    for position in sorted(snapshot, key=_scan_position_key):
        images = images_by_position.get(position)
        if not images:
            continue
        item = snapshot.get(position, {})

        overview = deck.add_slide("", kind="generic")
        _set_scan_title(deck, overview, sample, position, position in overridden)
        _add_captioned_picture(overview, "Band contrast", images.get("band_contrast"), 0.45, 0.65, 3.55, 2.45)
        _add_captioned_picture(overview, "Grain size (D)", images.get("grain_map"), 4.10, 0.65, 3.55, 2.45)
        grains = _finite(item.get("grains"))
        histogram_edges = _histogram_edges(grains, histogram_settings)
        histogram = _frontend_charts(
            [
                {
                    "sample": grains,
                    "golden": [],
                    "mode": "hist",
                    "histYMode": "ratio",
                    "fixedXMin": histogram_edges[0],
                    "fixedXMax": histogram_edges[-1],
                    "binWidth": histogram_edges[1] - histogram_edges[0],
                    "showXAxis": True,
                    "showYAxis": True,
                    "xAxisLabel": "Grain (µm)",
                    "maxXTicks": 5,
                }
            ]
        )[0]
        _add_captioned_picture(overview, "Grain-size distribution", histogram, 7.78, 0.65, 3.35, 2.45)
        _add_grain_summary_table(overview, item, 11.15, 1.02, 1.72, 1.55)
        histogram_note = _add_text(
            overview,
            _histogram_width_note(histogram_edges),
            Inches(11.15),
            Inches(2.62),
            Inches(1.72),
            Inches(0.32),
            size=8,
            color=MUTED,
            align=PP_ALIGN.CENTER,
        )
        histogram_note.name = "EBSD_HISTOGRAM_NOTE"

        ipf_maps = images.get("ipf_maps", {})
        ipf_items = list(ipf_maps.items())[:3] if isinstance(ipf_maps, dict) else []
        map_width = 11.25 / max(1, len(ipf_items))
        for index, (direction, image) in enumerate(ipf_items):
            _add_captioned_picture(
                overview,
                f"IPF-{direction}",
                image,
                0.45 + index * map_width,
                3.35,
                map_width - 0.25,
                3.35,
            )
        if ipf_legend:
            _add_captioned_picture(overview, "IPF color key", ipf_legend, 12.10, 5.22, 0.95, 1.20)

        if show_microstructure_page:
            _add_microstructure_page(deck, sample, position, images, position in overridden)

        texture = deck.add_slide("", kind="generic")
        _set_scan_title(deck, texture, sample, position, position in overridden)
        _add_text(texture, "Preferred orientation ratio", Inches(0.35), Inches(0.58), Inches(3.0), Inches(0.28), size=13, bold=True)
        _add_text(texture, "Direction: Z", Inches(0.45), Inches(0.88), Inches(1.5), Inches(0.24), size=10)
        # The C++ scale-bar footer makes these maps slightly taller than the
        # original image. Keep their picture boxes above the ratio tables so
        # PowerPoint never layers the table over the scale bar.
        labels = tuple(_orientation_labels({"_": item})[:3])
        deviation_20 = _add_picture_contain(
            texture,
            _deviation_legend(labels, 20),
            Inches(0.18),
            Inches(1.05),
            Inches(1.48),
            Inches(2.72),
        )
        deviation_20.name = "EBSD_DEVIATION_20"
        _add_captioned_picture(
            texture,
            "Range: 20°",
            images.get("preferred_20"),
            1.95,
            0.72,
            4.15,
            2.40,
            picture_name="EBSD_PREFERRED_20",
        )
        _add_captioned_picture(
            texture,
            "Range: 15°",
            images.get("preferred_15"),
            6.65,
            0.72,
            4.15,
            2.40,
            picture_name="EBSD_PREFERRED_15",
        )
        deviation_15 = _add_picture_contain(
            texture,
            _deviation_legend(labels, 15),
            Inches(11.70),
            Inches(1.05),
            Inches(1.45),
            Inches(2.28),
        )
        deviation_15.name = "EBSD_DEVIATION_15"
        _add_ratio_table(texture, item, "20%", 2.25, 3.18, 3.55, 0.72)
        _add_ratio_table(texture, item, "15%", 6.95, 3.18, 3.55, 0.72)
        if ipf_legend:
            _add_captioned_picture(
                texture,
                "Orientation key",
                ipf_legend,
                11.70,
                3.30,
                1.45,
                0.68,
                picture_name="EBSD_ORIENTATION_KEY",
            )
        _add_captioned_picture(texture, "Pole Figure", images.get("pole_figure"), 0.25, 4.05, 6.25, 3.15)
        _add_captioned_picture(texture, "Inverse Pole Figure", images.get("inverse_pole_figure"), 6.75, 4.05, 6.25, 3.15)


def _prepare_ipf_matrix(deck: TemplateDeck, slide, columns: Sequence[str], rows: Sequence[int]):
    """Create one centred responsive matrix; images and labels share its scale."""
    grid_shape = deck.shape(slide, "EBSD_IPF_GRID")
    prototype_table = grid_shape.table
    slide_width = deck.prs.slide_width
    slide_height = deck.prs.slide_height
    outer_left = Inches(0.34)
    outer_right = Inches(0.34)
    top = Inches(0.92)
    bottom = Inches(0.22)
    count_scale = min(1.0, 3.0 / max(1, len(columns)), 3.0 / max(1, len(rows)))
    # LibreOffice normalises ultra-short PowerPoint table rows to roughly
    # 0.34 in. Account for that height up front so images never cover the
    # column labels after export.
    header_column_width = Inches(max(0.20, 0.44 * count_scale))
    header_row_height = Inches(0.34)
    available_width = slide_width - outer_left - outer_right
    available_height = slide_height - top - bottom
    data_column_width = min(
        Inches(4.0),
        (available_width - header_column_width) // max(1, len(columns)),
    )
    data_row_height = min(
        Inches(2.05),
        (available_height - header_row_height) // max(1, len(rows)),
    )
    table_width = header_column_width + len(columns) * data_column_width
    table_height = header_row_height + len(rows) * data_row_height
    left = (slide_width - table_width) // 2
    top = top + max(0, (available_height - table_height) // 2)
    deck.remove_shape(slide, "EBSD_IPF_GRID")

    table = slide.shapes.add_table(
        len(rows) + 1,
        len(columns) + 1,
        left,
        top,
        table_width,
        table_height,
    ).table
    deck.apply_table_template(table, "ipf", "EBSD_IPF_GRID")
    _style_table_grid(table)
    table.columns[0].width = header_column_width
    for column_index, column in enumerate(table.columns):
        if column_index > 0:
            column.width = data_column_width
    table.rows[0].height = header_row_height
    for row_index, row in enumerate(table.rows):
        if row_index > 0:
            row.height = data_row_height

    visual_scale = min(
        1.0,
        data_column_width / Inches(4.0),
        data_row_height / Inches(2.05),
    )
    header_font = max(4, round(18 * visual_scale))
    _set_cell(table.cell(0, 0), "", size=header_font)
    for column_index, column in enumerate(columns, start=1):
        _set_cell(table.cell(0, column_index), column, header=True, size=header_font)
    for row_index, row in enumerate(rows, start=1):
        _set_cell(table.cell(row_index, 0), str(row), header=True, size=header_font)
        for column_index in range(1, len(columns) + 1):
            _set_cell(table.cell(row_index, column_index), "", size=header_font)

    horizontal_padding = Inches(0.06 * visual_scale)
    vertical_padding = Inches(0.04 * visual_scale)
    return (
        left + header_column_width,
        top + header_row_height,
        data_column_width,
        data_row_height,
        horizontal_padding,
        vertical_padding,
        visual_scale,
    )


def _add_ipf_pages(deck: TemplateDeck, selected_sample: str, version_label: str, directions: Dict[str, Dict[str, bytes]], legend: Optional[bytes]) -> None:
    for direction, images in directions.items():
        columns, rows = _matrix_positions(images)
        if not columns or not rows:
            continue
        slide = deck.add_slide(
            f"{selected_sample} · Orientation maps · IPF {direction.upper()}",
            f"{len(columns)} × {len(rows)} responsive matrix",
            kind="ipf",
        )
        image_left, image_top, cell_width, cell_height, pad_x, pad_y, _ = _prepare_ipf_matrix(
            deck, slide, columns, rows
        )
        for row_index, row in enumerate(rows):
            for column_index, column in enumerate(columns):
                image = images.get(f"{column}-{row}")
                if not image:
                    continue
                _add_picture_contain(
                    slide,
                    image,
                    image_left + column_index * cell_width + pad_x,
                    image_top + row_index * cell_height + pad_y,
                    cell_width - 2 * pad_x,
                    cell_height - 2 * pad_y,
                )


def _histogram_edges(all_values: Sequence[float], settings: Dict[str, Any]) -> List[float]:
    maximum = max(10.0, math.ceil(max(all_values, default=10.0) / 10.0) * 10.0)
    minimum = 0.0 if settings.get("start_mode") == "zero" else (math.floor(min(all_values) * 10) / 10 if all_values else 7.1)
    span = max(maximum - minimum, 0.1)
    if settings.get("width_mode") == "absolute":
        width = max(float(settings.get("width_absolute", 10.0)), 0.1)
    else:
        width = max(span * float(settings.get("width_ratio", 10.0)) / 100.0, 0.1)
    count = max(1, min(80, math.ceil(span / width)))
    edges = [minimum + index * width for index in range(count + 1)]
    edges[-1] = maximum
    return edges


def _add_grain_chart_pages(
    deck: TemplateDeck,
    selected: Snapshot,
    golden: Snapshot,
    golden_sample: str,
    golden_label: str,
    settings: Dict[str, Any],
) -> None:
    positions = _positions(selected, golden)
    all_values = [value for snapshot in (selected, golden) for item in snapshot.values() for value in _finite(item.get("grains"))]
    edges = _histogram_edges(all_values, settings)
    chart_mode = str(settings.get("chart_mode", "hist"))
    y_mode = str(settings.get("y_mode", "ratio"))
    if chart_mode == "areaHist":
        title = "Area-weighted grain histogram"
    elif chart_mode == "cdf":
        title = "Grain cumulative curve"
    else:
        chart_mode = "hist"
        title = f"Grain histogram ({'Ratio' if y_mode == 'ratio' else 'Count'})"
    matrix_columns, matrix_rows = _matrix_positions(positions)
    position_set = set(positions)
    if not matrix_columns or not matrix_rows:
        return
    subtitle = "Sample red dashed / Golden blue solid" if golden else "Sample red dashed"
    subtitle += f" · {len(matrix_columns)} × {len(matrix_rows)} responsive matrix"
    slide = deck.add_slide(title, subtitle, kind="ipf")
    notes = []
    if chart_mode != "cdf":
        notes.append(_histogram_width_note(edges))
    if golden:
        notes.append(_golden_data_note(golden_sample, golden_label))
    _add_report_note(slide, " | ".join(filter(None, notes)))
    image_left, image_top, cell_width, cell_height, pad_x, pad_y, visual_scale = _prepare_ipf_matrix(
        deck, slide, matrix_columns, matrix_rows
    )
    usable_width = max(1, cell_width - 2 * pad_x)
    usable_height = max(1, cell_height - 2 * pad_y)
    canvas_aspect_ratio = usable_width / usable_height
    chart_font_scale = min(2.2, max(1.0, 0.68 / max(visual_scale, 0.01)))
    placements: List[Tuple[int, int]] = []
    chart_requests: List[Dict[str, Any]] = []
    for row_index, row in enumerate(matrix_rows):
        for column_index, column in enumerate(matrix_columns):
            position = f"{column}-{row}"
            if position not in position_set:
                continue
            placements.append((column_index, row_index))
            chart_requests.append(
                {
                    "sample": _finite(selected.get(position, {}).get("grains")),
                    "golden": _finite(golden.get(position, {}).get("grains")),
                    "mode": chart_mode,
                    "histYMode": y_mode,
                    "fixedXMin": edges[0],
                    "fixedXMax": edges[-1],
                    "binWidth": edges[1] - edges[0],
                    "showXAxis": True,
                    "showYAxis": True,
                    "xAxisLabel": "Grain (µm)",
                    "maxXTicks": 6,
                    "canvasAspectRatio": canvas_aspect_ratio,
                    "fontScale": chart_font_scale,
                }
            )
    charts = _frontend_charts(
        chart_requests,
        shared_y_max=chart_mode != "cdf",
    )
    for (column_index, row_index), png in zip(placements, charts):
        _add_picture_contain(
            slide,
            png,
            image_left + column_index * cell_width + pad_x,
            image_top + row_index * cell_height + pad_y,
            cell_width - 2 * pad_x,
            cell_height - 2 * pad_y,
        )


def _add_complete_table(
    deck: TemplateDeck,
    sample: str,
    version: str,
    snapshot: Snapshot,
    overridden_positions: Sequence[str] = (),
) -> None:
    slide = deck.add_slide("Complete EBSD data", f"{sample} · {version}")
    positions = sorted(snapshot, key=_position_key)
    overridden_set = set(overridden_positions)
    labels = _orientation_labels(snapshot)
    columns = 10
    panel_count = 2 if len(positions) > 40 else 1
    per_panel = max(1, math.ceil(len(positions) / panel_count))
    chunks = [positions[index : index + per_panel] for index in range(0, len(positions), per_panel)] or [[]]
    total_width = Inches(12.72)
    panel_gap = Inches(0.14)
    panel_width = (total_width - panel_gap * (len(chunks) - 1)) // len(chunks)
    available_height = Inches(6.05)
    left_origin = (deck.prs.slide_width - total_width) // 2
    for panel_index, panel_positions in enumerate(chunks):
        column_width = panel_width // columns
        row_height = min(Inches(0.50), available_height // max(1, len(panel_positions) + 2))
        table_height = row_height * (len(panel_positions) + 2)
        left = left_origin + panel_index * (panel_width + panel_gap)
        top = Inches(0.88) + max(0, (available_height - table_height) // 2)
        visual_scale = min(1.0, column_width / Inches(1.24), row_height / Inches(0.46))
        header_font = max(5, round(17 * visual_scale))
        body_font = max(4, round(14 * visual_scale))
        table = slide.shapes.add_table(
            len(panel_positions) + 2,
            columns,
            left,
            top,
            panel_width,
            table_height,
        ).table
        deck.apply_table_template(table, "orientation_pixels", "EBSD_TABLE_20")
        _style_table_grid(table)
        for column in table.columns:
            column.width = column_width
        for row in table.rows:
            row.height = row_height
        for start, end, text in ((1, 3, "Grain Size"), (4, 6, "20°"), (7, 9, "15°")):
            table.cell(0, start).merge(table.cell(0, end))
            _set_cell(table.cell(0, start), text, header=True, size=header_font)
        _set_cell(table.cell(0, 0), "EBSD", header=True, size=header_font)
        headers = ["Pos.", "Avg µm", "Max µm", "Qty", *labels, *labels]
        for column_index, header in enumerate(headers):
            _set_cell(table.cell(1, column_index), header, header=True, size=body_font)
        for row_index, position in enumerate(panel_positions, start=2):
            item = snapshot.get(position, {})
            grains = _finite(item.get("grains"))
            ratio20 = _orientation_values(item, "20%")
            ratio15 = _orientation_values(item, "15%")
            values = [
                position,
                f"{statistics.mean(grains):.2f}" if grains else "—",
                f"{max(grains):.2f}" if grains else "—",
                str(len(grains)),
                *(f"{value:.1f}%" for value in ratio20),
                *(f"{value:.1f}%" for value in ratio15),
            ]
            for column_index, value in enumerate(values):
                _set_cell(
                    table.cell(row_index, column_index),
                    value,
                    header=column_index == 0,
                    size=body_font,
                    color=OVERRIDDEN_TEXT if column_index > 0 and position in overridden_set else None,
                )
        _scale_table_margins(table, visual_scale)


def _add_orientation_tables(
    deck: TemplateDeck,
    snapshot: Snapshot,
    *,
    grains: bool,
    overridden_positions: Sequence[str] = (),
) -> None:
    labels = _orientation_labels(snapshot)
    overridden_set = set(overridden_positions)
    kind = "orientation_grains" if grains else "orientation_pixels"
    title = "Orientation ratio by grains" if grains else "Orientation ratio"
    slide = deck.add_slide(title, kind=kind)
    deck.remove_shape(slide, "EBSD_TABLE_20")
    deck.remove_shape(slide, "EBSD_TABLE_15")
    deck.remove_shape(slide, "EBSD_SUBTITLE_20")
    deck.remove_shape(slide, "EBSD_SUBTITLE_15")
    columns, rows = _matrix_positions(snapshot)
    if not columns or not rows:
        return
    table_rows = len(rows) + 2
    column_count = 1 + len(columns) * 3
    available_width = Inches(12.55)
    column_width = available_width // column_count
    section_height = Inches(2.72)
    row_height = section_height // table_rows
    table_height = row_height * table_rows
    left = (deck.prs.slide_width - available_width) // 2
    top20 = Inches(1.02)
    top15 = Inches(4.12)
    visual_scale = min(1.0, column_width / Inches(0.992), row_height / Inches(0.406))
    font_size = max(3, round(18 * visual_scale))
    subtitle_font = max(8, round(16 * visual_scale))
    _add_text(
        slide,
        "Prefer orientation ratio (Misorientation angle 20°)",
        left,
        Inches(0.68),
        available_width,
        Inches(0.28),
        size=subtitle_font,
        bold=True,
        align=PP_ALIGN.CENTER,
    )
    _add_text(
        slide,
        "Prefer orientation ratio (Misorientation angle 15°)",
        left,
        Inches(3.78),
        available_width,
        Inches(0.28),
        size=subtitle_font,
        bold=True,
        align=PP_ALIGN.CENTER,
    )

    for deviation, top in (("20%", top20), ("15%", top15)):
        table = slide.shapes.add_table(
            table_rows,
            column_count,
            left,
            top,
            available_width,
            table_height,
        ).table
        deck.apply_table_template(table, kind, "EBSD_TABLE_20" if deviation == "20%" else "EBSD_TABLE_15")
        _style_table_grid(table)
        for column in table.columns:
            column.width = column_width
        for row in table.rows:
            row.height = row_height
        _set_cell(table.cell(0, 0), "", header=True, size=font_size)
        for axis_index, label in enumerate(labels):
            start = 1 + axis_index * len(columns)
            end = start + len(columns) - 1
            if end > start:
                table.cell(0, start).merge(table.cell(0, end))
            _set_cell(table.cell(0, start), label.strip("[]"), header=True, size=font_size)
            for column_index, column in enumerate(columns):
                _set_cell(table.cell(1, start + column_index), column, header=True, size=font_size)
        for row_index, row in enumerate(rows, start=2):
            _set_cell(table.cell(row_index, 0), str(row), header=True, size=font_size)
            for axis_index in range(3):
                for column_index, column in enumerate(columns):
                    position = f"{column}-{row}"
                    values = _orientation_values(snapshot.get(position), deviation, grains=grains)
                    _set_cell(
                        table.cell(row_index, 1 + axis_index * len(columns) + column_index),
                        f"{values[axis_index]:.1f}%" if position in snapshot else "",
                        size=font_size,
                        color=OVERRIDDEN_TEXT if position in overridden_set else None,
                    )
        _scale_table_margins(table, visual_scale)


def _add_statistics(
    deck: TemplateDeck,
    statistics_by_min_size: Dict[int, Snapshot],
    sample: str,
    overridden_positions: Sequence[str] = (),
) -> None:
    overridden_set = set(overridden_positions)
    metric_specs = (
        ("Average (µm)", "EBSD_STATS_AVERAGE", lambda values: f"{statistics.mean(values):.2f}" if values else "—"),
        ("Max (µm)", "EBSD_STATS_MAX", lambda values: f"{max(values):.2f}" if values else "—"),
        ("Quantity", "EBSD_STATS_QUANTITY", lambda values: str(len(values))),
    )
    for threshold in sorted(statistics_by_min_size):
        snapshot = statistics_by_min_size[threshold]
        columns, rows = _matrix_positions(snapshot)
        slide = deck.add_slide(f"Grain Size Summary · {sample}", kind="statistics")
        for name in ("EBSD_STATS_AVERAGE", "EBSD_STATS_MAX", "EBSD_STATS_QUANTITY"):
            deck.remove_shape(slide, name)
        if not columns or not rows:
            continue
        row_count = len(rows) + 2
        column_count = len(columns) + 1
        available_width = Inches(11.80)
        column_width = available_width // column_count
        section_height = Inches(1.88)
        row_height = section_height // row_count
        table_height = row_height * row_count
        left = (deck.prs.slide_width - available_width) // 2
        visual_scale = min(1.0, column_width / Inches(1.111), row_height / Inches(0.406))
        font_size = max(3, round(18 * visual_scale))
        for metric_index, (metric_name, style_shape, formatter) in enumerate(metric_specs):
            top = Inches(0.86) + metric_index * Inches(2.08)
            table = slide.shapes.add_table(
                row_count,
                column_count,
                left,
                top,
                available_width,
                table_height,
            ).table
            deck.apply_table_template(table, "statistics", style_shape)
            _style_table_grid(table)
            for column in table.columns:
                column.width = column_width
            for row in table.rows:
                row.height = row_height
            if column_count > 1:
                table.cell(0, 0).merge(table.cell(0, column_count - 1))
            _set_cell(table.cell(0, 0), f"Grain Size {metric_name}", header=True, size=font_size)
            _set_cell(table.cell(1, 0), "Position", header=True, size=font_size)
            for column_index, column in enumerate(columns, start=1):
                _set_cell(table.cell(1, column_index), column, header=True, size=font_size)
            for row_index, row in enumerate(rows, start=2):
                _set_cell(table.cell(row_index, 0), str(row), header=True, size=font_size)
                for column_index, column in enumerate(columns, start=1):
                    position = f"{column}-{row}"
                    grains = _finite(snapshot.get(position, {}).get("grains"))
                    _set_cell(
                        table.cell(row_index, column_index),
                        formatter(grains) if position in snapshot else "",
                        size=font_size,
                        color=OVERRIDDEN_TEXT if position in overridden_set else None,
                    )
            _scale_table_margins(table, visual_scale)


def _triangle_chart(
    position: str,
    selected_item: Optional[Dict[str, Any]],
    golden_item: Optional[Dict[str, Any]],
    deviation: str,
    labels: List[str],
    chart_max: float,
    *,
    show_value_labels: bool = True,
    show_axis_labels: bool = True,
    density_scale: float = 1.0,
) -> io.BytesIO:
    """Render one scan position using the same triangle system as TriangleChart.vue."""
    figure = Figure(figsize=(3.75, 2.5), dpi=220)
    canvas = FigureCanvasAgg(figure)
    axis = figure.add_subplot(111)
    density_scale = max(0.1, min(1.0, float(density_scale)))
    # Dense one-slide N x M matrices shrink the chart bitmap substantially.
    # Boost text and strokes from the actual cell scale so there is no special
    # case or upper bound tied to any example matrix size.
    density_boost = min(1.85, max(1.0, 0.60 / density_scale))
    stroke_boost = min(1.4, density_boost)
    vertices = [(0.0, 1.0), (math.sqrt(3) / 2, -0.5), (-math.sqrt(3) / 2, -0.5)]

    def points(values: Sequence[float]) -> List[Tuple[float, float]]:
        return [
            (vertices[index][0] * float(value) / chart_max, vertices[index][1] * float(value) / chart_max)
            for index, value in enumerate(values[:3])
        ]

    for level in (chart_max / 5, chart_max * 2 / 5, chart_max * 3 / 5, chart_max * 4 / 5, chart_max):
        polygon = points([level, level, level])
        polygon.append(polygon[0])
        axis.plot([point[0] for point in polygon], [point[1] for point in polygon], color="#d1d5db", linewidth=0.8 * stroke_boost)
    for vertex in vertices:
        axis.plot([0, vertex[0]], [0, vertex[1]], color="#9ca3af", linewidth=0.8 * stroke_boost)

    series = (
        ("Sample", selected_item, ORIENTATION_SAMPLE, False),
        ("Golden", golden_item, ORIENTATION_GOLDEN, True),
    )
    for source, item, color, dashed in series:
        if item is None:
            continue
        values = _orientation_values(item, deviation)
        polygon = points(values)
        closed = polygon + polygon[:1]
        axis.plot(
            [point[0] for point in closed],
            [point[1] for point in closed],
            color=color,
            linewidth=1.35 * stroke_boost,
            linestyle="--" if dashed else "-",
            label=source,
        )
        axis.fill([point[0] for point in closed], [point[1] for point in closed], color=color, alpha=0.06)
        axis.scatter(
            [point[0] for point in polygon],
            [point[1] for point in polygon],
            color=color,
            s=13 * stroke_boost * stroke_boost,
        )

    # Treat every label as a measured collision box.  Axis labels and the
    # outer-scale label reserve space first; values then walk a radial search
    # and take the closest free position.  This works for any N x M matrix
    # because it depends on the actual rendered font size, not a fixed zone.
    x_limits = (-1.34, 1.34)
    y_limits = (-1.06, 1.44)
    label_requests: List[Dict[str, Any]] = []
    value_requests: List[Dict[str, Any]] = []
    visible_series = [entry for entry in series if entry[1] is not None]

    for axis_index, (label, vertex) in enumerate(zip(labels, vertices)):
        length = math.hypot(*vertex) or 1.0
        outward = (vertex[0] / length, vertex[1] / length)
        tangent = (-outward[1], outward[0])
        if show_axis_labels:
            label_requests.append(
                {
                    "text": label,
                    "color": "#374151",
                    "fontsize": 12.0 * density_boost,
                    "weight": "bold",
                    "preferred": (
                        vertex[0] + outward[0] * 0.25,
                        vertex[1] + outward[1] * 0.25,
                    ),
                    "background": False,
                    "outlined": False,
                    "halo": False,
                }
            )
        if not show_value_labels:
            continue
        for series_index, (_source, item, color, _dashed) in enumerate(visible_series):
            values = _orientation_values(item, deviation)
            endpoint = points(values)[axis_index]
            centered_index = series_index - (len(visible_series) - 1) / 2.0
            value_requests.append(
                {
                    "text": f"{values[axis_index]:.1f}%",
                    "color": color,
                    "fontsize": 8.5 * density_boost,
                    "weight": "bold",
                    "preferred": (
                        endpoint[0] + outward[0] * 0.14 + tangent[0] * centered_index * 0.19,
                        endpoint[1] + outward[1] * 0.14 + tangent[1] * centered_index * 0.19,
                    ),
                    "background": False,
                    "outlined": False,
                    "halo": True,
                }
            )

    top, right = vertices[0], vertices[1]
    scale_midpoint = ((top[0] + right[0]) / 2.0, (top[1] + right[1]) / 2.0)
    scale_length = math.hypot(*scale_midpoint) or 1.0
    label_requests.append(
        {
            "text": f"{chart_max:g}%",
            "color": "#6b7280",
            "fontsize": 8.5 * density_boost,
            "weight": "bold",
            "preferred": (
                scale_midpoint[0] + scale_midpoint[0] / scale_length * 0.16,
                scale_midpoint[1] + scale_midpoint[1] / scale_length * 0.16,
            ),
            "background": False,
            "outlined": False,
            "halo": True,
        }
    )

    candidate_offsets = [(0.0, 0.0)]
    search_radius = 0.035
    while search_radius <= 0.84:
        candidate_count = max(8, math.ceil(2.0 * math.pi * search_radius / 0.065))
        candidate_offsets.extend(
            (
                search_radius * math.cos(2.0 * math.pi * index / candidate_count),
                search_radius * math.sin(2.0 * math.pi * index / candidate_count),
            )
            for index in range(candidate_count)
        )
        search_radius += 0.035

    endpoint_obstacles = [
        {"x": point[0], "y": point[1], "width": 0.14, "height": 0.14}
        for _source, item, _color, _dashed in visible_series
        for point in points(_orientation_values(item, deviation))
    ]
    placed_labels: List[Dict[str, Any]] = []
    for request in [*label_requests, *value_requests]:
        padding = 0.07 if request["background"] else 0.035 if request["halo"] else 0.0
        width = max(
            request["fontsize"] * 0.016,
            len(request["text"]) * request["fontsize"] * 0.012,
        ) + padding
        height = request["fontsize"] * 0.021 + padding
        best = None
        best_score = math.inf
        for offset_x, offset_y in candidate_offsets:
            raw_x = request["preferred"][0] + offset_x
            raw_y = request["preferred"][1] + offset_y
            x = min(x_limits[1] - width / 2.0, max(x_limits[0] + width / 2.0, raw_x))
            y = min(y_limits[1] - height / 2.0, max(y_limits[0] + height / 2.0, raw_y))
            overlap = 0.0
            for placed in [*endpoint_obstacles, *placed_labels]:
                overlap_width = max(
                    0.0,
                    min(x + width / 2.0, placed["x"] + placed["width"] / 2.0)
                    - max(x - width / 2.0, placed["x"] - placed["width"] / 2.0)
                    + 0.050,
                )
                overlap_height = max(
                    0.0,
                    min(y + height / 2.0, placed["y"] + placed["height"] / 2.0)
                    - max(y - height / 2.0, placed["y"] - placed["height"] / 2.0)
                    + 0.050,
                )
                overlap += overlap_width * overlap_height
            displacement = (x - request["preferred"][0]) ** 2 + (y - request["preferred"][1]) ** 2
            score = overlap * 10000.0 + displacement
            if score < best_score:
                best_score = score
                best = {**request, "x": x, "y": y, "width": width, "height": height}
            if overlap == 0.0:
                break
        if best is not None:
            placed_labels.append(best)

    for placed in placed_labels:
        text = axis.text(
            placed["x"],
            placed["y"],
            placed["text"],
            fontsize=placed["fontsize"],
            fontweight=placed["weight"],
            color=placed["color"],
            ha="center",
            va="center",
            bbox=(
                {"boxstyle": "round,pad=0.20", "facecolor": "white", "edgecolor": "#d1d5db", "linewidth": 0.5, "alpha": 0.96}
                if placed["outlined"]
                else {"boxstyle": "round,pad=0.16", "facecolor": "white", "edgecolor": "none", "linewidth": 0.0, "alpha": 0.90}
                if placed["background"]
                else None
            ),
            zorder=10,
        )
        if placed["halo"]:
            text.set_path_effects([patheffects.withStroke(linewidth=1.5, foreground="white")])

    # Once any N x M cell becomes dense enough, the page's column and row
    # headers already identify every position.  Dropping the duplicate A-1 /
    # B-1 title gives the labels more usable height without losing information.
    if density_scale >= 0.45:
        axis.set_title(position, fontsize=12 * density_boost, fontweight="bold", pad=2)
    axis.set_aspect("equal", adjustable="box")
    axis.set_xlim(*x_limits)
    axis.set_ylim(*y_limits)
    axis.axis("off")
    figure.tight_layout(pad=0.30)
    output = io.BytesIO()
    canvas.print_png(output)
    output.seek(0)
    return output


def _add_triangle_pages(
    deck: TemplateDeck,
    selected: Snapshot,
    golden: Snapshot,
    golden_sample: str,
    golden_version_label: str,
    *,
    show_value_labels: bool = True,
) -> None:
    positions = _positions(selected, golden)
    labels = _orientation_labels(selected)
    columns, rows = _matrix_positions(positions)
    if not columns or not rows:
        return
    position_set = set(positions)
    for deviation in ("20%", "15%"):
        all_values = [
            value
            for snapshot in (selected, golden)
            for position in positions
            for value in _orientation_values(snapshot.get(position), deviation)
        ]
        chart_max = _triangle_percent_max(all_values)
        slide = deck.add_slide(
            f"晶粒取向比例 - Misorientation {deviation}",
            (
                f"Sample 藍色實線 / Golden 橘色虛線 / Scale 0–{chart_max:g}%"
                if golden
                else f"Sample 藍色實線 / Scale 0–{chart_max:g}%"
            ),
            kind="ipf",
        )
        image_left, image_top, cell_width, cell_height, pad_x, pad_y, visual_scale = _prepare_ipf_matrix(
            deck, slide, columns, rows
        )
        show_axis_labels, effective_value_labels = _triangle_annotation_mode(
            visual_scale,
            show_value_labels,
        )
        if show_value_labels and not effective_value_labels:
            subtitle_shape = next(
                (shape for shape in slide.shapes if shape.name == "EBSD_SUBTITLE"),
                None,
            )
            if subtitle_shape is not None:
                paragraph = subtitle_shape.text_frame.paragraphs[0]
                note_run = paragraph.add_run()
                note_run.text = " / Values: data tables"
                note_run.font.size = Pt(10)
                note_run.font.color.rgb = MUTED
        for row_index, row in enumerate(rows):
            for column_index, column in enumerate(columns):
                position = f"{column}-{row}"
                if position not in position_set:
                    continue
                png = _triangle_chart(
                    position,
                    selected.get(position),
                    golden.get(position),
                    deviation,
                    labels,
                    chart_max,
                    show_value_labels=effective_value_labels,
                    show_axis_labels=show_axis_labels,
                    density_scale=visual_scale,
                )
                _add_picture_contain(
                    slide,
                    png,
                    image_left + column_index * cell_width + pad_x,
                    image_top + row_index * cell_height + pad_y,
                    cell_width - 2 * pad_x,
                    cell_height - 2 * pad_y,
                )
        subtitle_shape = next((shape for shape in slide.shapes if shape.name == "EBSD_SUBTITLE"), None)
        if subtitle_shape is not None:
            slide.shapes._spTree.remove(subtitle_shape._element)
            slide.shapes._spTree.append(subtitle_shape._element)
        if golden:
            _add_report_note(slide, _golden_data_note(golden_sample, golden_version_label))


def _line_chart(
    columns: Sequence[str],
    selected_values: Sequence[float],
    golden_values: Optional[Sequence[float]],
    axis_label: str,
    y_max: float,
    *,
    show_value_labels: bool = True,
    aspect_ratio: float = 2.0,
    show_x_axis: bool = True,
    show_y_axis: bool = True,
) -> io.BytesIO:
    figure_width = 4.0
    figure = Figure(figsize=(figure_width, max(0.72, figure_width / max(1.0, aspect_ratio))), dpi=220)
    canvas = FigureCanvasAgg(figure)
    axis = figure.add_subplot(111)
    x = list(range(len(columns)))
    density_scale = min(1.0, 5.0 / max(1, len(columns)))
    label_size = max(6.5, 9.0 * density_scale)
    marker_size = max(2.5, 4.5 * density_scale)
    value_stride = _label_stride(len(columns), 10)
    tick_stride = _label_stride(len(columns), 12)
    value_indices = set(_sampled_label_indices(len(columns), value_stride))
    tick_indices = _sampled_label_indices(len(columns), tick_stride)
    axis.plot(x, selected_values, color=ORIENTATION_SAMPLE, marker="o", markersize=marker_size, linewidth=2.0, label="Sample")
    if golden_values is not None:
        axis.plot(x, golden_values, color=ORIENTATION_GOLDEN, marker="o", markersize=marker_size, linewidth=2.0, linestyle="--", label="Golden")
    if show_value_labels:
        for index, value in enumerate(selected_values):
            if index not in value_indices:
                continue
            axis.annotate(f"{value:.1f}%", (index, value), xytext=(0, 6 + (index % 2) * 6), textcoords="offset points", ha="center", fontsize=label_size, color=ORIENTATION_SAMPLE, fontweight="bold")
        if golden_values is not None:
            for index, value in enumerate(golden_values):
                if index not in value_indices:
                    continue
                axis.annotate(f"{value:.1f}%", (index, value), xytext=(0, -10 - (index % 2) * 6), textcoords="offset points", ha="center", fontsize=label_size, color=ORIENTATION_GOLDEN, fontweight="bold")
    axis.set_xticks(
        tick_indices,
        [columns[index] if show_x_axis else "" for index in tick_indices],
        rotation=0,
    )
    axis.set_ylim(0, y_max)
    axis.set_yticks([y_max * index / 5 for index in range(6)])
    axis.set_ylabel("Ratio %" if show_y_axis else "", fontsize=11)
    if not show_y_axis:
        axis.tick_params(axis="y", labelleft=False)
        axis.spines["left"].set_visible(False)
    if axis_label:
        axis.set_title(axis_label, fontsize=12, pad=4)
    axis.grid(axis="y", color="#eeeeee")
    axis.tick_params(labelsize=10)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)
    figure.tight_layout(pad=0.55)
    output = io.BytesIO()
    canvas.print_png(output)
    output.seek(0)
    return output


def _add_orientation_lines(
    deck: TemplateDeck,
    selected: Snapshot,
    golden: Snapshot,
    golden_sample: str,
    golden_version_label: str,
    *,
    y_max: Optional[float] = None,
    show_value_labels: bool = True,
) -> None:
    positions = _positions(selected, golden)
    labels = _orientation_labels(selected)
    columns, rows = _matrix_positions(positions)
    for deviation in ("20%", "15%"):
        all_values = [
            value
            for snapshot in (selected, golden)
            for position in positions
            for value in _orientation_values(snapshot.get(position), deviation)
        ]
        chart_y_max = float(y_max) if y_max is not None else _nice_percent_max(all_values)
        value_stride = _label_stride(len(columns), 10)
        density_note = (
            f" / Dense matrix: value labels every {value_stride} points"
            if show_value_labels and value_stride > 1
            else ""
        )
        slide = deck.add_slide(
            f"晶粒取向折線圖 - Misorientation {deviation}",
            (
                "Sample 藍色實線 / Golden 橘色虛線"
                if golden
                else "Sample 藍色實線"
            ) + density_note,
            kind="ipf",
        )
        image_left, image_top, cell_width, cell_height, pad_x, pad_y, _ = _prepare_ipf_matrix(
            deck,
            slide,
            labels,
            rows,
        )
        for row_index, row in enumerate(rows):
            for axis_index, label in enumerate(labels):
                selected_values = [
                    _orientation_values(selected.get(f"{column}-{row}"), deviation)[axis_index]
                    for column in columns
                ]
                golden_values = (
                    [
                        _orientation_values(golden.get(f"{column}-{row}"), deviation)[axis_index]
                        for column in columns
                    ]
                    if golden
                    else None
                )
                png = _line_chart(
                    columns,
                    selected_values,
                    golden_values,
                    "",
                    chart_y_max,
                    show_value_labels=show_value_labels,
                    aspect_ratio=cell_width / max(1, cell_height),
                    show_x_axis=row_index == len(rows) - 1,
                    show_y_axis=axis_index == 0,
                )
                _add_picture_contain(
                    slide,
                    png,
                    image_left + axis_index * cell_width + pad_x,
                    image_top + row_index * cell_height + pad_y,
                    cell_width - 2 * pad_x,
                    cell_height - 2 * pad_y,
                )
        subtitle_shape = next((shape for shape in slide.shapes if shape.name == "EBSD_SUBTITLE"), None)
        if subtitle_shape is not None:
            slide.shapes._spTree.remove(subtitle_shape._element)
            slide.shapes._spTree.append(subtitle_shape._element)
        if golden:
            _add_report_note(slide, _golden_data_note(golden_sample, golden_version_label))


def _analysis_color(value: float, thresholds: Sequence[float]) -> RGBColor:
    low, high = (list(thresholds) + [30.0, 70.0])[:2]
    absolute = abs(value)
    if absolute <= low:
        return RGBColor(16, 185, 129)
    if absolute >= high:
        return RGBColor(239, 68, 68)
    return RGBColor(245, 158, 11)


def _add_feature_analysis(
    deck: TemplateDeck,
    analysis: Dict[str, Dict[str, float]],
    selected_sample: str,
    golden_sample: str,
    golden_version_label: str,
    features: Sequence[str],
    thresholds: Sequence[float],
) -> None:
    values = analysis.get(selected_sample, {})
    if not values or not features:
        return
    slide = deck.add_slide("Feature analysis", "Features: " + ", ".join(features))
    _add_report_note(slide, _golden_data_note(golden_sample, golden_version_label))
    positions = sorted(values, key=_position_key)
    _add_native_matrix(deck, slide, positions, lambda position: f"{values[position] * 100:.1f}%", data_column_width=0.9, row_height=0.42)
    # Recolour only data cells, preserving the native editable table.
    table_shape = next(shape for shape in reversed(slide.shapes) if getattr(shape, "has_table", False))
    table = table_shape.table
    columns, rows = _matrix_positions(positions)
    for row_index, row in enumerate(rows, start=1):
        for column_index, column in enumerate(columns, start=1):
            position = f"{column}-{row}"
            if position not in values:
                continue
            table.cell(row_index, column_index).fill.solid()
            table.cell(row_index, column_index).fill.fore_color.rgb = _analysis_color(float(values[position]) * 100, thresholds)


def build_pptx_report(
    *,
    selected_sample: str,
    golden_sample: str,
    selected_version_label: str,
    golden_version_label: str,
    report_data: ReportData,
    ipf_display_sample: Optional[str] = None,
    grid_display_sample: Optional[str] = None,
    grid_display_version_label: Optional[str] = None,
    ipf_images_by_direction: Optional[Dict[str, Dict[str, bytes]]] = None,
    ipf_images: Optional[Dict[str, bytes]] = None,
    ipf_legend: Optional[bytes] = None,
    scan_report_images: Optional[Dict[str, Dict[str, Any]]] = None,
    statistics_by_min_grain_size: Optional[Dict[int, Snapshot]] = None,
    analysis_data: Optional[Dict[str, Dict[str, float]]] = None,
    analysis_features: Optional[List[str]] = None,
    color_thresholds: Optional[List[float]] = None,
    overridden_positions: Optional[List[str]] = None,
    histogram_settings: Optional[Dict[str, Any]] = None,
    orientation_line_y_max: Optional[float] = None,
    show_microstructure_page: bool = False,
    show_triangle_labels: bool = True,
    show_orientation_line_labels: bool = True,
) -> bytes:
    deck = TemplateDeck()
    selected = report_data.get(selected_sample, {})
    golden = report_data.get(golden_sample, {}) if golden_sample else {}
    directions = dict(ipf_images_by_direction or {})
    if not directions and ipf_images:
        directions["z"] = ipf_images
    overwritten = overridden_positions or []
    _add_scan_point_pages(
        deck,
        selected_sample,
        selected,
        scan_report_images or {},
        overwritten,
        ipf_legend,
        histogram_settings or {},
        show_microstructure_page=show_microstructure_page,
    )
    _add_ipf_pages(deck, ipf_display_sample or selected_sample, selected_version_label, directions, ipf_legend)
    _add_grain_chart_pages(deck, selected, golden, golden_sample, golden_version_label, histogram_settings or {})
    grid_sample = grid_display_sample or selected_sample
    grid_snapshot = report_data.get(grid_sample, selected)
    _add_complete_table(
        deck,
        grid_sample,
        grid_display_version_label or selected_version_label,
        grid_snapshot,
        overwritten if grid_sample == selected_sample else (),
    )
    _add_orientation_tables(deck, selected, grains=False, overridden_positions=overwritten)
    _add_statistics(deck, statistics_by_min_grain_size or {10: selected}, selected_sample, overwritten)
    _add_triangle_pages(
        deck,
        selected,
        golden,
        golden_sample,
        golden_version_label,
        show_value_labels=show_triangle_labels,
    )
    _add_orientation_lines(
        deck,
        selected,
        golden,
        golden_sample,
        golden_version_label,
        y_max=orientation_line_y_max,
        show_value_labels=show_orientation_line_labels,
    )
    _add_feature_analysis(
        deck,
        analysis_data or {},
        selected_sample,
        golden_sample,
        golden_version_label,
        analysis_features or [],
        color_thresholds or [30.0, 70.0],
    )
    return deck.save()
