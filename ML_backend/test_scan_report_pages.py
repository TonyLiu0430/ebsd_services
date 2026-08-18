import io
import unittest

import numpy as np
from PIL import Image
from diffpy.structure import Lattice, Structure
from orix.crystal_map import Phase
from orix.quaternion import Orientation, symmetry
from pptx import Presentation
from pptx.dml.color import RGBColor

from python_ebsd_figures import _inverse_pole_figure, _pole_figure
from template_ppt_report import build_pptx_report


def _test_png() -> bytes:
    stream = io.BytesIO()
    Image.new("RGB", (160, 100), (120, 180, 210)).save(stream, format="PNG")
    return stream.getvalue()


def _scan_images() -> dict:
    image = _test_png()
    return {
        "band_contrast": image,
        "grain_map": image,
        "preferred_20": image,
        "preferred_15": image,
        "pole_figure": image,
        "inverse_pole_figure": image,
        "ipf_maps": {"X": image, "Y": image, "Z": image},
    }


class ScanPointPageTests(unittest.TestCase):
    def test_each_position_gets_two_pages_and_only_overwrite_is_reanalysis(self) -> None:
        item = {
            "grains": [4.0, 7.0, 11.0],
            "orientation_labels": ["[001]", "[011]", "[111]"],
            "orientation_ratio(20%)": [0.1, 0.2, 0.3],
            "orientation_ratio(15%)": [0.05, 0.1, 0.15],
        }
        report = build_pptx_report(
            selected_sample="Cu-U",
            golden_sample="",
            selected_version_label="2",
            golden_version_label="",
            report_data={"Cu-U": {"A-1": item, "B-2": item}},
            scan_report_images={"A-1": _scan_images(), "B-2": _scan_images()},
            overridden_positions=["A-1"],
        )
        presentation = Presentation(io.BytesIO(report))
        scan_titles = []
        for slide in presentation.slides:
            title = next((shape for shape in slide.shapes if shape.name == "EBSD_TITLE"), None)
            if title is not None and title.text.startswith("Cu-U "):
                scan_titles.append(title)

        self.assertEqual([title.text for title in scan_titles].count("Cu-U A-1 (Reanalysis)"), 2)
        self.assertEqual([title.text for title in scan_titles].count("Cu-U B-2"), 2)
        self.assertFalse(any("Reanalysis" in title.text for title in scan_titles if "B-2" in title.text))

        reanalysis_runs = [
            run
            for title in scan_titles
            if "A-1" in title.text
            for paragraph in title.text_frame.paragraphs
            for run in paragraph.runs
            if run.text == " (Reanalysis)"
        ]
        self.assertEqual(len(reanalysis_runs), 2)
        self.assertTrue(all(run.font.color.rgb == RGBColor(0, 0, 255) for run in reanalysis_runs))


class PythonTextureFigureTests(unittest.TestCase):
    def test_orix_generates_pf_and_ipf_png(self) -> None:
        phase = Phase(
            name="Cu",
            point_group=symmetry.Oh,
            structure=Structure(
                lattice=Lattice(a=3.6, b=3.6, c=3.6, alpha=90, beta=90, gamma=90)
            ),
        )
        rng = np.random.default_rng(7)
        euler = rng.random((300, 3)) * np.array([2 * np.pi, np.pi, 2 * np.pi])
        orientations = Orientation.from_euler(
            euler,
            symmetry=symmetry.Oh,
            direction="lab2crystal",
        )

        pole_figure = _pole_figure(orientations, "cubic", phase)
        inverse_pole_figure = _inverse_pole_figure(orientations, symmetry.Oh)

        self.assertEqual(pole_figure[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(inverse_pole_figure[:8], b"\x89PNG\r\n\x1a\n")
        self.assertGreater(len(pole_figure), 10_000)
        self.assertGreater(len(inverse_pole_figure), 10_000)


if __name__ == "__main__":
    unittest.main()
