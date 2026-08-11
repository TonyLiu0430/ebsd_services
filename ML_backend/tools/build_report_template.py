#!/usr/bin/env python3
"""Build the version-controlled EBSD report template from a visual reference PPTX.

The reference file itself must never be copied into the repository.  This script
retains only its theme/master/layout parts and replaces every slide with a clean,
named prototype slide that contains no customer images, tables, or report data.
"""

from __future__ import annotations

import argparse
import os
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


REL_NS = "http://schemas.openxmlformats.org/package/2006/relationships"
P_NS = "http://schemas.openxmlformats.org/presentationml/2006/main"
R_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
CT_NS = "http://schemas.openxmlformats.org/package/2006/content-types"
A_NS = "http://schemas.openxmlformats.org/drawingml/2006/main"

PROTOTYPES = (
    ("EBSD_TEMPLATE_IPF", "IPF map"),
    ("EBSD_TEMPLATE_ORIENTATION_PIXELS", "Orientation ratio (pixels)"),
    ("EBSD_TEMPLATE_ORIENTATION_GRAINS", "Orientation ratio (grains)"),
    ("EBSD_TEMPLATE_STATISTICS", "Grain statistics"),
    ("EBSD_TEMPLATE_GENERIC", "Report content"),
)

SLIDE_NAMES = {index: name for index, (name, _) in enumerate(PROTOTYPES, start=1)}

SKIP_PREFIXES = (
    "ppt/media/",
    "ppt/notesSlides/",
    "ppt/notesMasters/",
    "ppt/comments/",
    "ppt/commentAuthors.xml",
    "ppt/tags/",
)


def _slide_xml(prototype_name: str, label: str) -> bytes:
    # Coordinates are EMU and target the original 16:9 page size.
    xml = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
       xmlns:r="{R_NS}" xmlns:p="{P_NS}">
  <p:cSld name="{prototype_name}"><p:spTree>
    <p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>
    <p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>
    {_text_shape(2, prototype_name, prototype_name, 548640, 137160, 11094720, 411480, 2200, True, "172033")}
    {_text_shape(3, "EBSD_TITLE", label, 548640, 594360, 11094720, 502920, 2400, True, "172033")}
    {_text_shape(4, "EBSD_SUBTITLE", "Runtime subtitle", 548640, 1036320, 11094720, 274320, 1000, False, "667085")}
    {_content_shape()}
    {_text_shape(6, "EBSD_FOOTER", "EBSD automated report", 548640, 6537960, 11094720, 182880, 800, False, "98A2B3")}
  </p:spTree></p:cSld>
  <p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>
</p:sld>'''
    return xml.encode("utf-8")


def _set_shape_name(element: ET.Element, name: str) -> None:
    non_visual = element.find(f".//{{{P_NS}}}cNvPr")
    if non_visual is not None:
        non_visual.set("name", name)


def _set_shape_text(element: ET.Element, value: str) -> None:
    texts = list(element.iter(f"{{{A_NS}}}t"))
    for text in texts:
        text.text = ""
    if texts:
        texts[0].text = value


def _sanitize_source_slide(data: bytes, slide_number: int) -> bytes:
    """Keep the supplied slide geometry/style while removing customer content."""
    root = ET.fromstring(data)
    common_slide = root.find(f"{{{P_NS}}}cSld")
    if common_slide is None:
        raise RuntimeError(f"slide {slide_number} has no cSld")
    common_slide.set("name", SLIDE_NAMES[slide_number])
    shape_tree = common_slide.find(f"{{{P_NS}}}spTree")
    if shape_tree is None:
        raise RuntimeError(f"slide {slide_number} has no shape tree")

    # Customer images are never copied. Their exact source geometry is recorded
    # in the manifest and reused by the runtime renderer.
    for child in list(shape_tree):
        if child.tag == f"{{{P_NS}}}pic":
            shape_tree.remove(child)

    tables = [child for child in list(shape_tree) if child.tag == f"{{{P_NS}}}graphicFrame"]
    text_shapes = [child for child in list(shape_tree) if child.tag == f"{{{P_NS}}}sp"]
    for table in tables:
        _set_shape_text(table, "")
    for shape in text_shapes:
        _set_shape_text(shape, "")

    if slide_number == 1:
        if tables:
            _set_shape_name(tables[0], "EBSD_IPF_GRID")
        if text_shapes:
            _set_shape_name(text_shapes[0], "EBSD_TITLE")
            _set_shape_text(text_shapes[0], "Orientation maps with IPF Z")
    elif slide_number in (2, 3):
        for table, name in zip(tables, ("EBSD_TABLE_20", "EBSD_TABLE_15")):
            _set_shape_name(table, name)
        for shape, name, value in zip(
            text_shapes,
            ("EBSD_TITLE", "EBSD_SUBTITLE_20", "EBSD_SUBTITLE_15"),
            (
                "Orientation ratio by pixels" if slide_number == 2 else "Orientation ratio by grains",
                "Prefer orientation ratio (Misorientation angle 20°)",
                "Prefer orientation ratio (Misorientation angle 15°)",
            ),
        ):
            _set_shape_name(shape, name)
            _set_shape_text(shape, value)
    elif slide_number == 4:
        for table, name in zip(tables, ("EBSD_STATS_AVERAGE", "EBSD_STATS_MAX", "EBSD_STATS_QUANTITY")):
            _set_shape_name(table, name)
        if text_shapes:
            _set_shape_name(text_shapes[0], "EBSD_TITLE")
            _set_shape_text(text_shapes[0], "Grain Size")
    else:
        # Slide five is sourced from slide two by the caller. It becomes a
        # plain report page that retains the supplied title/subtitle typography.
        for table in tables:
            shape_tree.remove(table)
        for shape in text_shapes[2:]:
            shape_tree.remove(shape)
        if text_shapes:
            _set_shape_name(text_shapes[0], "EBSD_TITLE")
            _set_shape_text(text_shapes[0], "Report title")
        if len(text_shapes) > 1:
            _set_shape_name(text_shapes[1], "EBSD_SUBTITLE")
            _set_shape_text(text_shapes[1], "Report subtitle")

    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _text_shape(
    shape_id: int,
    name: str,
    text: str,
    x: int,
    y: int,
    cx: int,
    cy: int,
    size: int,
    bold: bool,
    color: str,
) -> str:
    bold_attr = ' b="1"' if bold else ""
    return f'''<p:sp>
      <p:nvSpPr><p:cNvPr id="{shape_id}" name="{name}"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr>
      <p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/><a:ln><a:noFill/></a:ln></p:spPr>
      <p:txBody><a:bodyPr wrap="square" anchor="ctr"/><a:lstStyle/><a:p><a:r><a:rPr lang="zh-TW" sz="{size}"{bold_attr}><a:solidFill><a:srgbClr val="{color}"/></a:solidFill><a:latin typeface="Microsoft JhengHei"/><a:ea typeface="Microsoft JhengHei"/></a:rPr><a:t>{text}</a:t></a:r><a:endParaRPr lang="zh-TW"/></a:p></p:txBody>
    </p:sp>'''


def _content_shape() -> str:
    return '''<p:sp>
      <p:nvSpPr><p:cNvPr id="5" name="EBSD_CONTENT"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr>
      <p:spPr><a:xfrm><a:off x="548640" y="1371600"/><a:ext cx="11094720" cy="4937760"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val="F8FAFC"/></a:solidFill><a:ln w="12700"><a:solidFill><a:srgbClr val="D0D5DD"/></a:solidFill><a:prstDash val="dash"/></a:ln></p:spPr>
      <p:txBody><a:bodyPr anchor="ctr"/><a:lstStyle/><a:p><a:pPr algn="ctr"/><a:r><a:rPr lang="en-US" sz="1000"><a:solidFill><a:srgbClr val="98A2B3"/></a:solidFill><a:latin typeface="Microsoft JhengHei"/></a:rPr><a:t>EBSD_CONTENT</a:t></a:r><a:endParaRPr/></a:p></p:txBody>
    </p:sp>'''


def _clean_relationships(data: bytes, *, keep_type_suffix: str) -> bytes:
    root = ET.fromstring(data)
    for rel in list(root):
        if not rel.attrib.get("Type", "").endswith(keep_type_suffix):
            root.remove(rel)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _clean_content_types(data: bytes) -> bytes:
    root = ET.fromstring(data)
    for item in list(root):
        part = item.attrib.get("PartName", "")
        extension = item.attrib.get("Extension", "").lower()
        if "/notesSlides/" in part or "/notesMasters/" in part or extension in {"png", "jpg", "jpeg", "gif", "tiff", "emf", "wmf"}:
            root.remove(item)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _clean_presentation_relationships(data: bytes) -> bytes:
    root = ET.fromstring(data)
    for rel in list(root):
        if rel.attrib.get("Type", "").endswith("/notesMaster"):
            root.remove(rel)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _clean_presentation(data: bytes) -> bytes:
    root = ET.fromstring(data)
    for child in list(root):
        if child.tag == f"{{{P_NS}}}notesMasterIdLst":
            root.remove(child)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _scrub_layout_text(data: bytes) -> bytes:
    root = ET.fromstring(data)
    for text in root.iter(f"{{{A_NS}}}t"):
        text.text = ""
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _clean_core_properties() -> bytes:
    return b'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:dcterms="http://purl.org/dc/terms/"
 xmlns:dcmitype="http://purl.org/dc/dcmitype/"
 xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
 <dc:title>EBSD report template</dc:title><dc:creator>EBSD Services</dc:creator>
 <cp:lastModifiedBy>EBSD Services</cp:lastModifiedBy><cp:revision>1</cp:revision>
</cp:coreProperties>'''


def _clean_app_properties() -> bytes:
    return b'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties">
 <Application>EBSD Services</Application><PresentationFormat>Widescreen</PresentationFormat>
 <Slides>5</Slides><Notes>0</Notes><HiddenSlides>0</HiddenSlides><MMClips>0</MMClips>
 <ScaleCrop>false</ScaleCrop><Company></Company><LinksUpToDate>false</LinksUpToDate>
 <SharedDoc>false</SharedDoc><HyperlinksChanged>false</HyperlinksChanged><AppVersion>1.0</AppVersion>
</Properties>'''


def _clean_root_relationships(data: bytes) -> bytes:
    root = ET.fromstring(data)
    for rel in list(root):
        if rel.attrib.get("Type", "").endswith("/thumbnail"):
            root.remove(rel)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def build_template(source: Path, output: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as src:
        names = set(src.namelist())
        for index in range(1, len(PROTOTYPES) + 1):
            required = f"ppt/slides/_rels/slide{index}.xml.rels"
            if required not in names:
                raise RuntimeError(f"reference PPTX does not have required slide {index}")
        generic_slide_data = src.read("ppt/slides/slide2.xml")

        fd, temporary_name = tempfile.mkstemp(prefix="ebsd-template-", suffix=".pptx", dir=output.parent)
        os.close(fd)
        try:
            with zipfile.ZipFile(temporary_name, "w", compression=zipfile.ZIP_DEFLATED) as dst:
                for info in src.infolist():
                    name = info.filename
                    if name == "docProps/thumbnail.jpeg" or name.startswith(SKIP_PREFIXES):
                        continue
                    data = src.read(name)
                    if name == "[Content_Types].xml":
                        data = _clean_content_types(data)
                    elif name == "_rels/.rels":
                        data = _clean_root_relationships(data)
                    elif name == "docProps/core.xml":
                        data = _clean_core_properties()
                    elif name == "docProps/app.xml":
                        data = _clean_app_properties()
                    elif name == "ppt/presentation.xml":
                        data = _clean_presentation(data)
                    elif name == "ppt/_rels/presentation.xml.rels":
                        data = _clean_presentation_relationships(data)
                    elif name.startswith("ppt/slideMasters/") and name.endswith(".xml"):
                        data = _scrub_layout_text(data)
                    elif name.startswith("ppt/slideLayouts/") and name.endswith(".xml"):
                        data = _scrub_layout_text(data)
                    elif name.startswith("ppt/slides/slide") and name.endswith(".xml"):
                        index = int(name.removeprefix("ppt/slides/slide").removesuffix(".xml"))
                        if 1 <= index <= len(PROTOTYPES):
                            # Generic slide uses the exact title/subtitle shapes
                            # from the supplied orientation page, not a new style.
                            if index == 5:
                                data = generic_slide_data
                            data = _sanitize_source_slide(data, index)
                    elif name.startswith("ppt/slides/_rels/slide") and name.endswith(".xml.rels"):
                        data = _clean_relationships(data, keep_type_suffix="/slideLayout")
                    dst.writestr(info, data)
            os.replace(temporary_name, output)
            # The ML image runs as UID 999. mkstemp creates mode 0600, which
            # would become an unreadable root-owned file after Docker COPY.
            output.chmod(0o644)
        finally:
            if os.path.exists(temporary_name):
                os.unlink(temporary_name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="customer visual reference PPTX (never copied)")
    parser.add_argument("--output", type=Path, required=True, help="clean generated template PPTX")
    args = parser.parse_args()
    build_template(args.source, args.output)


if __name__ == "__main__":
    main()
