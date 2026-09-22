#!/usr/bin/env python3
"""Compare Python and C++ PF/IPF output using real CPR/CRC scans.

The report intentionally measures only projected/coloured pixels in addition
to the full frame. This prevents a matching white background from hiding a
wrong projection, scale, or panel layout.
"""

from __future__ import annotations

import argparse
import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np
from PIL import Image
from defdap import ebsd
from orix.vector import Vector3d

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ML_backend"))

from python_ebsd_figures import (  # noqa: E402
    _channel5_cubic_ipf_density,
    _families,
    _inverse_pole_figure,
    _lambert_equal_area_density,
    _orix_phase,
    _orientations,
    _pole_figure,
    _texture_subset,
)


def read_density(path: Path) -> List[np.ndarray]:
    lines = path.read_text().splitlines()
    maps: List[np.ndarray] = []
    offset = 1
    for panel in range(3):
        header = lines[offset].split(",")
        if header[:2] != ["panel", str(panel)]:
            raise RuntimeError(f"Invalid density header in {path}: {header}")
        width, height = int(header[2]), int(header[3])
        values = np.fromstring(lines[offset + 1], sep=",", dtype=float)
        values[values < 0] = np.nan
        maps.append(values.reshape(height, width))
        offset += 2
    return maps


def density_metrics(reference: np.ndarray, actual: np.ndarray) -> Dict[str, float]:
    valid = np.isfinite(reference) & np.isfinite(actual)
    left = reference[valid]
    right = actual[valid]
    scale = max(float(np.std(left)), 0.05)
    return {
        "correlation": float(np.corrcoef(left, right)[0, 1]),
        "normalized_mae": float(np.mean(np.abs(left - right)) / scale),
        "python_min": float(np.min(left)),
        "python_max": float(np.max(left)),
        "cpp_min": float(np.min(right)),
        "cpp_max": float(np.max(right)),
    }


def image_metrics(reference_png: bytes, actual_path: Path) -> Dict[str, float]:
    reference = np.asarray(Image.open(io.BytesIO(reference_png)).convert("RGB"), dtype=float) / 255.0
    actual = np.asarray(Image.open(actual_path).convert("RGB"), dtype=float) / 255.0
    if reference.shape != actual.shape:
        raise RuntimeError(f"Image size mismatch: Python={reference.shape}, C++={actual.shape}")
    reference_mask = (reference.max(axis=2) - reference.min(axis=2)) > 0.20
    actual_mask = (actual.max(axis=2) - actual.min(axis=2)) > 0.20
    union = reference_mask | actual_mask
    intersection = reference_mask & actual_mask
    return {
        "whole_image_mae": float(np.mean(np.abs(reference - actual))),
        "foreground_mae": float(np.mean(np.abs(reference - actual)[union])),
        "foreground_iou": float(np.sum(intersection) / max(np.sum(union), 1)),
        "foreground_rgb_correlation": float(
            np.corrcoef(reference[intersection].ravel(), actual[intersection].ravel())[0, 1]
        ),
    }


def phase_family(ebsd_map: ebsd.Map) -> str:
    lattice = tuple(float(value) for value in ebsd_map.primary_phase.lattice_params)
    gamma_degrees = np.degrees(lattice[5])
    relative_ab = abs(lattice[0] - lattice[1]) / max((lattice[0] + lattice[1]) * 0.5, 1e-12)
    return "hcp" if relative_ab <= 0.03 and abs(gamma_degrees - 120.0) <= 3.0 else "cubic"


def passes_thresholds(
    pf_density: Sequence[Dict[str, float]],
    ipf_density: Sequence[Dict[str, float]],
    pf_image: Dict[str, float],
    ipf_image: Dict[str, float],
) -> bool:
    density = list(pf_density) + list(ipf_density)
    return (
        all(item["correlation"] >= 0.9999 for item in density)
        and all(item["normalized_mae"] <= 0.001 for item in density)
        and pf_image["foreground_mae"] <= 0.05
        and ipf_image["foreground_mae"] <= 0.05
        and pf_image["foreground_iou"] >= 0.97
        and ipf_image["foreground_iou"] >= 0.97
        and pf_image["foreground_rgb_correlation"] >= 0.985
        and ipf_image["foreground_rgb_correlation"] >= 0.985
    )


def compare_scan(cpr_path: Path, data_root: Path, output_root: Path, fixture: Path) -> Dict[str, Any]:
    relative = cpr_path.relative_to(data_root).with_suffix("")
    output = output_root / relative
    cpp_output = output / "cpp"
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(fixture), str(cpr_path), str(cpp_output)], check=True)

    ebsd_map = ebsd.Map(str(cpr_path.with_suffix("")), data_type="OxfordBinary")
    family = phase_family(ebsd_map)
    point_group, phase = _orix_phase(ebsd_map, family)
    orientations, _ = _orientations(ebsd_map, point_group)
    orientations = _texture_subset(orientations)

    families, _ = _families(family, phase)
    python_pf_density = [
        np.asarray(
            _lambert_equal_area_density(
                orientations.inv().outer(item, lazy=False, progressbar=False)
            ).filled(np.nan),
            dtype=float,
        )
        for item in families
    ]
    cpp_pf_density = read_density(cpp_output / "pole-density.csv")
    pf_density = [
        density_metrics(reference, actual)
        for reference, actual in zip(python_pf_density, cpp_pf_density)
    ]

    ipf_density: List[Dict[str, float]] = []
    if family == "cubic":
        directions = (Vector3d.xvector(), Vector3d.yvector(), Vector3d.zvector())
        python_ipf_density = [
            _channel5_cubic_ipf_density(orientations * direction)[2]
            for direction in directions
        ]
        cpp_ipf_density = read_density(cpp_output / "inverse-density.csv")
        ipf_density = [
            density_metrics(reference, actual)
            for reference, actual in zip(python_ipf_density, cpp_ipf_density)
        ]

    python_pf = _pole_figure(orientations, family, phase)
    python_ipf = _inverse_pole_figure(orientations, point_group)
    (output / "python-pole.png").write_bytes(python_pf)
    (output / "python-inverse.png").write_bytes(python_ipf)
    pf_image = image_metrics(python_pf, cpp_output / "pole.png")
    ipf_image = image_metrics(python_ipf, cpp_output / "inverse.png")
    passed = passes_thresholds(pf_density, ipf_density, pf_image, ipf_image)
    return {
        "scan": str(relative),
        "family": family,
        "orientation_count": int(orientations.size),
        "pf_density": pf_density,
        "ipf_density": ipf_density,
        "pf_image": pf_image,
        "ipf_image": ipf_image,
        "passed": passed,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("data_root", nargs="?", type=Path, default=ROOT / ".temp")
    parser.add_argument("--output", type=Path, default=ROOT / ".temp" / "texture-comparison")
    parser.add_argument(
        "--fixture",
        type=Path,
        default=ROOT / "cpp_backend" / "build" / "default" / "texture_real_data_fixture",
    )
    args = parser.parse_args()
    scans = sorted(args.data_root.glob("**/*.cpr"))
    if not scans:
        parser.error(f"No CPR files found under {args.data_root}")
    if not args.fixture.is_file():
        parser.error(f"C++ fixture not found: {args.fixture}")

    results: List[Dict[str, Any]] = []
    for index, cpr_path in enumerate(scans, start=1):
        print(f"[{index}/{len(scans)}] comparing {cpr_path.relative_to(args.data_root)}", flush=True)
        result = compare_scan(cpr_path, args.data_root, args.output, args.fixture)
        results.append(result)
        print(
            "  "
            f"PF corr={min(item['correlation'] for item in result['pf_density']):.4f}, "
            f"IPF corr={min((item['correlation'] for item in result['ipf_density']), default=1.0):.4f}, "
            f"PF fg-MAE={result['pf_image']['foreground_mae']:.4f}, "
            f"IPF fg-MAE={result['ipf_image']['foreground_mae']:.4f}, "
            f"passed={result['passed']}",
            flush=True,
        )

    summary = {
        "data_root": str(args.data_root.resolve()),
        "scan_count": len(results),
        "passed_count": sum(bool(item["passed"]) for item in results),
        "failed_count": sum(not bool(item["passed"]) for item in results),
        "results": results,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    report_path = args.output / "report.json"
    report_path.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"report: {report_path}", flush=True)
    return 0 if summary["failed_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
