"""Cross-language regression test for the C++ PF/IPF migration.

The old Python implementation remains importable as a test oracle, but the
production report path no longer calls it.  Build ``texture_parity_fixture``
before running this test; ``EBSD_TEXTURE_FIXTURE`` can override its location.
"""

from __future__ import annotations

import csv
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import List, Tuple

import numpy as np
from diffpy.structure import Lattice, Structure
from PIL import Image
from orix.crystal_map import Phase
from orix.quaternion import Orientation, symmetry
from orix.vector import Vector3d

from python_ebsd_figures import (
    _channel5_cubic_ipf_density,
    _families,
    _inverse_pole_figure,
    _lambert_equal_area_density,
    _pole_figure,
)


ROOT = Path(__file__).resolve().parents[1]


def _fixture_path() -> Path:
    configured = os.getenv("EBSD_TEXTURE_FIXTURE")
    candidates = [
        Path(configured) if configured else None,
        ROOT / "cpp_backend" / "build" / "default" / "texture_parity_fixture",
        ROOT / "cpp_backend" / "build" / "release" / "texture_parity_fixture",
    ]
    for candidate in candidates:
        if candidate and candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise unittest.SkipTest(
        "Build cpp_backend target texture_parity_fixture or set EBSD_TEXTURE_FIXTURE"
    )


def _read_density(path: Path) -> Tuple[float, float, List[np.ndarray]]:
    with path.open(newline="") as stream:
        rows = csv.reader(stream)
        limits = next(rows)
        low, high = float(limits[0]), float(limits[1])
        maps: List[np.ndarray] = []
        for panel in range(3):
            header = next(rows)
            if header[:2] != ["panel", str(panel)]:
                raise AssertionError(f"invalid C++ density header: {header}")
            width, height = int(header[2]), int(header[3])
            values = np.asarray([float(value) for value in next(rows)], dtype=float)
            values[values < 0] = np.nan
            maps.append(values.reshape(height, width))
    return low, high, maps


def _density_similarity(reference: np.ndarray, actual: np.ndarray) -> Tuple[float, float]:
    valid = np.isfinite(reference) & np.isfinite(actual)
    reference_values = reference[valid]
    actual_values = actual[valid]
    correlation = float(np.corrcoef(reference_values, actual_values)[0, 1])
    scale = max(float(np.std(reference_values)), 0.05)
    normalized_mae = float(np.mean(np.abs(reference_values - actual_values)) / scale)
    return correlation, normalized_mae


def _image_metrics(reference_png: bytes, actual_path: Path) -> Tuple[float, float, float]:
    reference = np.asarray(Image.open(io.BytesIO(reference_png)).convert("RGB"), dtype=float) / 255.0
    actual = np.asarray(Image.open(actual_path).convert("RGB"), dtype=float) / 255.0
    if reference.shape != actual.shape:
        raise AssertionError(f"image sizes differ: Python={reference.shape}, C++={actual.shape}")

    # Whole-frame MAE catches layout regressions.  A colour histogram metric
    # remains stable across unavoidable font rasterizer and contour-edge
    # differences while still comparing the produced image, not just metadata.
    mae = float(np.mean(np.abs(reference - actual)))
    reference_color = reference[(reference.max(axis=2) - reference.min(axis=2)) > 0.20]
    actual_color = actual[(actual.max(axis=2) - actual.min(axis=2)) > 0.20]
    coverage_ratio = min(len(reference_color), len(actual_color)) / max(
        len(reference_color), len(actual_color), 1
    )
    bins = np.linspace(0.0, 1.0, 17)
    reference_hist, _ = np.histogram(reference_color.ravel(), bins=bins, density=False)
    actual_hist, _ = np.histogram(actual_color.ravel(), bins=bins, density=False)
    reference_hist = reference_hist / max(reference_hist.sum(), 1)
    actual_hist = actual_hist / max(actual_hist.sum(), 1)
    histogram_l1 = float(np.sum(np.abs(reference_hist - actual_hist)))
    return mae, coverage_ratio, histogram_l1


def _foreground_image_metrics(
    reference_png: bytes, actual_path: Path
) -> Tuple[float, float, float]:
    reference = np.asarray(
        Image.open(io.BytesIO(reference_png)).convert("RGB"), dtype=float
    ) / 255.0
    actual = np.asarray(Image.open(actual_path).convert("RGB"), dtype=float) / 255.0
    reference_mask = (reference.max(axis=2) - reference.min(axis=2)) > 0.20
    actual_mask = (actual.max(axis=2) - actual.min(axis=2)) > 0.20
    union = reference_mask | actual_mask
    intersection = reference_mask & actual_mask
    foreground_mae = float(np.mean(np.abs(reference - actual)[union]))
    iou = float(np.sum(intersection) / max(np.sum(union), 1))
    rgb_correlation = float(
        np.corrcoef(reference[intersection].ravel(), actual[intersection].ravel())[0, 1]
    )
    return foreground_mae, iou, rgb_correlation


class TextureCppParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fixture = _fixture_path()
        cls.phase = Phase(
            name="Cu",
            point_group=symmetry.Oh,
            structure=Structure(
                lattice=Lattice(a=3.6, b=3.6, c=3.6, alpha=90, beta=90, gamma=90)
            ),
        )

    def _compare_seed(self, seed: int) -> None:
        rng = np.random.default_rng(seed)
        euler = rng.random((360, 3)) * np.asarray([2 * np.pi, np.pi, 2 * np.pi])
        orientations = Orientation.from_euler(
            euler,
            symmetry=symmetry.Oh,
            direction="lab2crystal",
        )

        with tempfile.TemporaryDirectory(prefix=f"texture-parity-{seed}-") as directory:
            output = Path(directory)
            input_path = output / "euler.csv"
            with input_path.open("w", newline="") as stream:
                stream.write("cubic\n")
                writer = csv.writer(stream)
                writer.writerows(euler)
            subprocess.run(
                [str(self.fixture), str(input_path), str(output)],
                check=True,
                capture_output=True,
                text=True,
            )

            families, _ = _families("cubic", self.phase)
            python_pf = [
                np.asarray(
                    _lambert_equal_area_density(
                        orientations.inv().outer(item, lazy=False, progressbar=False)
                    ).filled(np.nan),
                    dtype=float,
                )
                for item in families
            ]
            _, _, cpp_pf = _read_density(output / "pole-density.csv")
            for panel, (reference, actual) in enumerate(zip(python_pf, cpp_pf)):
                correlation, normalized_mae = _density_similarity(reference, actual)
                self.assertGreater(correlation, 0.9999, f"seed={seed}, PF panel={panel}")
                self.assertLess(normalized_mae, 0.001, f"seed={seed}, PF panel={panel}")

            directions = (Vector3d.xvector(), Vector3d.yvector(), Vector3d.zvector())
            python_ipf = [
                _channel5_cubic_ipf_density(orientations * direction)[2]
                for direction in directions
            ]
            _, _, cpp_ipf = _read_density(output / "inverse-density.csv")
            for panel, (reference, actual) in enumerate(zip(python_ipf, cpp_ipf)):
                correlation, normalized_mae = _density_similarity(reference, actual)
                self.assertGreater(correlation, 0.9999, f"seed={seed}, IPF panel={panel}")
                self.assertLess(normalized_mae, 0.001, f"seed={seed}, IPF panel={panel}")

            for name, reference_png, actual_path in (
                ("PF", _pole_figure(orientations, "cubic", self.phase), output / "pole.png"),
                ("IPF", _inverse_pole_figure(orientations, symmetry.Oh), output / "inverse.png"),
            ):
                mae, coverage_ratio, histogram_l1 = _image_metrics(reference_png, actual_path)
                self.assertLess(mae, 0.06, f"seed={seed}, {name} whole-image MAE")
                self.assertGreater(coverage_ratio, 0.95, f"seed={seed}, {name} colour coverage")
                self.assertLess(histogram_l1, 0.65, f"seed={seed}, {name} colour histogram")
                foreground_mae, iou, rgb_correlation = _foreground_image_metrics(
                    reference_png, actual_path
                )
                self.assertLess(foreground_mae, 0.06, f"seed={seed}, {name} foreground MAE")
                self.assertGreater(iou, 0.97, f"seed={seed}, {name} foreground IoU")
                self.assertGreater(
                    rgb_correlation, 0.98, f"seed={seed}, {name} foreground RGB correlation"
                )

    def test_seed_7_matches_python_reference(self) -> None:
        self._compare_seed(7)

    def test_seed_20260919_matches_python_reference(self) -> None:
        self._compare_seed(20260919)

    def test_hcp_pf_and_ipf_images_match_python_reference(self) -> None:
        rng = np.random.default_rng(19)
        euler = rng.random((360, 3)) * np.asarray([2 * np.pi, np.pi, 2 * np.pi])
        orientations = Orientation.from_euler(
            euler,
            symmetry=symmetry.D6h,
            direction="lab2crystal",
        )
        phase = Phase(
            name="Ti",
            point_group=symmetry.D6h,
            structure=Structure(
                lattice=Lattice(a=2.95, b=2.95, c=4.68, alpha=90, beta=90, gamma=120)
            ),
        )

        with tempfile.TemporaryDirectory(prefix="texture-parity-hcp-") as directory:
            output = Path(directory)
            input_path = output / "euler.csv"
            with input_path.open("w", newline="") as stream:
                stream.write("hcp\n")
                csv.writer(stream).writerows(euler)
            subprocess.run(
                [str(self.fixture), str(input_path), str(output)],
                check=True,
                capture_output=True,
                text=True,
            )

            families, _ = _families("hcp", phase)
            python_pf = [
                np.asarray(
                    _lambert_equal_area_density(
                        orientations.inv().outer(item, lazy=False, progressbar=False)
                    ).filled(np.nan),
                    dtype=float,
                )
                for item in families
            ]
            _, _, cpp_pf = _read_density(output / "pole-density.csv")
            for panel, (reference, actual) in enumerate(zip(python_pf, cpp_pf)):
                correlation, normalized_mae = _density_similarity(reference, actual)
                self.assertGreater(correlation, 0.58, f"HCP PF panel={panel}")
                self.assertLess(normalized_mae, 0.80, f"HCP PF panel={panel}")

            pf_metrics = _image_metrics(
                _pole_figure(orientations, "hcp", phase), output / "pole.png"
            )
            self.assertLess(pf_metrics[0], 0.16, "HCP PF whole-image MAE")
            self.assertGreater(pf_metrics[1], 0.65, "HCP PF colour coverage")
            self.assertLess(pf_metrics[2], 0.25, "HCP PF colour histogram")

            # Orix's hexagonal IPF renderer uses a different contour
            # tessellation and the standard jet map, so image tolerances are
            # intentionally looser than the cubic Channel 5-specific path.
            ipf_metrics = _image_metrics(
                _inverse_pole_figure(orientations, symmetry.D6h), output / "inverse.png"
            )
            self.assertLess(ipf_metrics[0], 0.22, "HCP IPF whole-image MAE")
            self.assertGreater(ipf_metrics[1], 0.45, "HCP IPF colour coverage")
            self.assertLess(ipf_metrics[2], 1.10, "HCP IPF colour histogram")


if __name__ == "__main__":
    unittest.main()
