#!/usr/bin/env python3
"""Generate deterministic responsive-layout PPT fixtures for visual review."""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from template_ppt_report import build_pptx_report  # noqa: E402


def excel_column(value: int) -> str:
    result = ""
    while value:
        value -= 1
        result = chr(65 + value % 26) + result
        value //= 26
    return result


def position_image(position: str, index: int) -> bytes:
    width, height = 720, 420
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    for y in range(height - 48):
        ratio = y / max(1, height - 49)
        colour = (
            round(40 + 160 * ratio),
            round(185 - 90 * ratio),
            round(225 - 120 * ratio),
        )
        draw.line((0, y, width, y), fill=colour)
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 54)
    draw.rounded_rectangle((24, 24, 230, 104), radius=18, fill=(255, 255, 255), outline=(55, 65, 81), width=4)
    draw.text((127, 64), position, font=font, fill=(17, 24, 39), anchor="mm")
    draw.rectangle((0, height - 48, width, height), fill=(250, 250, 250))
    draw.rectangle((40, height - 34, 240, height - 24), fill=(20, 20, 20))
    draw.text((270, height - 29), f"{index + 1} · responsive fixture", fill=(35, 35, 35), anchor="lm")
    output = io.BytesIO()
    image.save(output, format="PNG")
    return output.getvalue()


def snapshot(columns: int, rows: int, *, golden: bool = False):
    result = {}
    images = {}
    for row in range(1, rows + 1):
        for column in range(1, columns + 1):
            position = f"{excel_column(column)}-{row}"
            index = (row - 1) * columns + column - 1
            base = 7.0 + (index % 11) * 2.25 + (1.1 if golden else 0.0)
            grains = [base + offset * 3.2 for offset in range(18)]
            peak = 0.60 if index % 7 == 0 else 0.42
            result[position] = {
                "grains": grains,
                "orientation_labels": ["[001]", "[011]", "[111]"],
                "orientation_ratio(20%)": [0.12 + index % 4 * 0.03, 0.24 + index % 5 * 0.025, peak],
                "orientation_ratio(15%)": [0.08 + index % 3 * 0.025, 0.17 + index % 4 * 0.02, 0.31 + index % 6 * 0.02],
            }
            images[position] = position_image(position, index)
    return result, images


def generate(columns: int, rows: int, output: Path) -> None:
    selected, images = snapshot(columns, rows)
    golden, _ = snapshot(columns, rows, golden=True)
    preview = images["A-1"]
    scan_report_images = {
        "A-1": {
            "band_contrast": preview,
            "grain_map": preview,
            "grain_boundaries": preview,
            "csl_map": preview,
            "csl_summary": {"Σ3": 12.5, "Σ5": 6.4, "Σ7": 3.1},
            "kam_map": preview,
            "kam_values": [0.35, 0.52, 0.71, 1.15, 1.42, 2.05],
            "gos_map": preview,
            "gos_values": [0.41, 0.63, 0.88, 1.22, 1.71, 2.44],
            "preferred_20": preview,
            "preferred_15": preview,
            "pole_figure": preview,
            "inverse_pole_figure": preview,
            "ipf_maps": {"Z": preview},
        }
    }
    data = build_pptx_report(
        selected_sample="Responsive-Sample",
        golden_sample="Responsive-Golden",
        selected_version_label="Responsive-Sample-2",
        golden_version_label="Responsive-Golden-1",
        report_data={"Responsive-Sample": selected, "Responsive-Golden": golden},
        ipf_images_by_direction={"Z": images},
        scan_report_images=scan_report_images,
        histogram_settings={
            "start_mode": "zero",
            "width_mode": "absolute",
            "width_absolute": 10.0,
            "chart_mode": "hist",
            "y_mode": "ratio",
        },
    )
    output.write_bytes(data)


def parse_size(value: str) -> tuple[int, int]:
    parts = value.lower().replace("×", "x").replace("*", "x").split("x", 1)
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("size must look like NxM, for example 11x6")
    try:
        columns, rows = (int(part.strip()) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("N and M must be positive integers") from exc
    if columns < 1 or rows < 1:
        raise argparse.ArgumentTypeError("N and M must be positive integers")
    return columns, rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("/tmp/ebsd-ppt-fixtures"))
    parser.add_argument(
        "--size",
        action="append",
        type=parse_size,
        dest="sizes",
        help="matrix size NxM; repeat for any number of layouts",
    )
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for columns, rows in args.sizes or ((3, 3), (5, 4), (8, 9)):
        path = args.out / f"report-{columns}x{rows}.pptx"
        generate(columns, rows, path)
        print(path)


if __name__ == "__main__":
    main()
