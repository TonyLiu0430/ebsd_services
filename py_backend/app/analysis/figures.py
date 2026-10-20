from __future__ import annotations

import math
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np
from defdap import ebsd
from diffpy.structure import Lattice, Structure
from matplotlib.colors import LinearSegmentedColormap
from orix.crystal_map import Phase
from orix.quaternion import Orientation, symmetry
from orix.vector import Miller, Vector3d


CHANNEL5_DENSITY_CMAP = LinearSegmentedColormap.from_list(
    "channel5_density",
    (
        (0.00, (0.0, 0.0, 1.0)),
        (0.34, (0.0, 1.0, 0.0)),
        (0.72, (1.0, 1.0, 0.0)),
        (1.00, (1.0, 0.0, 0.0)),
    ),
)
PREFERRED_COLORS = np.asarray(
    (
        (0.95, 0.36, 0.36),
        (0.35, 0.82, 0.39),
        (0.40, 0.45, 0.90),
    ),
    dtype=float,
)


@dataclass(frozen=True)
class RawRgbMap:
    pixels: bytes
    width: int
    height: int
    x_step_um: float
    map_name: str


def _raw_rgb_map(rgb: np.ndarray, step_size: float, map_name: str) -> RawRgbMap:
    values = np.asarray(rgb)
    if values.ndim != 3 or values.shape[2] != 3:
        raise ValueError("Scale-bar maps must be RGB images")
    if values.dtype == np.uint8:
        pixels = np.ascontiguousarray(values)
    else:
        pixels = np.rint(np.clip(values, 0.0, 1.0) * 255.0).astype(np.uint8)
    height, width = pixels.shape[:2]
    return RawRgbMap(
        pixels=pixels.tobytes(),
        width=width,
        height=height,
        x_step_um=float(step_size),
        map_name=map_name,
    )


def _normalised_band_contrast(ebsd_map: ebsd.Map) -> np.ndarray:
    values = np.asarray(ebsd_map.data.band_contrast, dtype=float)
    finite = values[np.isfinite(values)]
    if finite.size == 0:
        return np.zeros(ebsd_map.shape, dtype=float)
    low, high = np.percentile(finite, (1, 99))
    if high <= low:
        high = low + 1.0
    return np.clip((values - low) / (high - low), 0.0, 1.0)


def _grain_boundaries(grains: np.ndarray) -> np.ndarray:
    boundary = np.zeros(grains.shape, dtype=bool)
    boundary[:, 1:] |= grains[:, 1:] != grains[:, :-1]
    boundary[1:, :] |= grains[1:, :] != grains[:-1, :]
    return boundary


def _grain_size_rgb(grains: np.ndarray, step_size: float) -> np.ndarray:
    """Colour grains by equivalent-circle diameter using a Channel 5-like scale."""
    rgb = np.full(grains.shape + (3,), 0.93, dtype=float)
    indexed = grains > 0
    if np.any(indexed):
        _, inverse, pixel_counts = np.unique(
            grains[indexed],
            return_inverse=True,
            return_counts=True,
        )
        pixel_area = max(float(step_size), 0.0) ** 2
        diameters = 2.0 * np.sqrt(pixel_counts * pixel_area / math.pi)
        maximum = float(np.max(diameters))
        normalised = diameters / maximum if maximum > 0 else np.zeros_like(diameters)
        grain_colours = CHANNEL5_DENSITY_CMAP(np.clip(normalised, 0.0, 1.0))[:, :3]
        rgb[indexed] = grain_colours[inverse]

    rgb[~indexed] = 0.0
    rgb[_grain_boundaries(grains)] = 0.05
    return rgb


def _grain_map(ebsd_map: ebsd.Map, min_grain_size: int) -> Tuple[RawRgbMap, np.ndarray]:
    # Access through Datastore so DefDAP stores the generated boundary set
    # before find_grains() consumes it.
    _ = ebsd_map.data.grain_boundaries
    # DefDAP indexes every grain first. Filtering here avoids its empty-list
    # edge case when every grain is smaller than the report threshold.
    grains = np.asarray(ebsd_map.find_grains(min_grain_size=1), dtype=int)
    grain_ids, grain_sizes = np.unique(grains[grains > 0], return_counts=True)
    for grain_id in grain_ids[grain_sizes < max(1, min_grain_size)]:
        grains[grains == grain_id] = -2
    rgb = _grain_size_rgb(grains, float(ebsd_map.step_size))
    return _raw_rgb_map(rgb, float(ebsd_map.step_size), "GS"), grains


def _scalar_rgb(values: np.ndarray, valid: np.ndarray) -> np.ndarray:
    source = np.asarray(values, dtype=float)
    mask = np.asarray(valid, dtype=bool) & np.isfinite(source)
    rgb = np.zeros(source.shape + (3,), dtype=float)
    finite = source[mask]
    if finite.size:
        low = float(np.percentile(finite, 1))
        high = float(np.percentile(finite, 99))
        if high <= low:
            high = low + 1.0
        normalized = np.clip((source - low) / (high - low), 0.0, 1.0)
        rgb[mask] = CHANNEL5_DENSITY_CMAP(normalized[mask])[:, :3]
    return rgb


def _kam_figure(ebsd_map: ebsd.Map, valid: np.ndarray) -> Tuple[RawRgbMap, List[float]]:
    degrees = np.degrees(np.asarray(ebsd_map.data.KAM, dtype=float))
    values = degrees[np.asarray(valid, dtype=bool) & np.isfinite(degrees)]
    return (
        _raw_rgb_map(_scalar_rgb(degrees, valid), float(ebsd_map.step_size), "KAM"),
        values.tolist(),
    )


def _grain_orientation_stats(
    orientations: Orientation,
    valid: np.ndarray,
    grains: np.ndarray,
) -> Tuple[np.ndarray, List[float], Dict[int, np.ndarray]]:
    quaternion_grid = np.zeros(grains.shape + (4,), dtype=float)
    quaternion_grid[np.asarray(valid, dtype=bool)] = np.asarray(orientations.data, dtype=float).reshape(-1, 4)
    gos_map = np.full(grains.shape, np.nan, dtype=float)
    grain_values: List[float] = []
    grain_means: Dict[int, np.ndarray] = {}
    for grain_id in sorted(int(value) for value in np.unique(grains) if value > 0):
        values = quaternion_grid[grains == grain_id]
        if not values.size:
            continue
        reference = values[0]
        aligned = values.copy()
        aligned[np.einsum("ij,j->i", aligned, reference) < 0] *= -1.0
        mean = np.mean(aligned, axis=0)
        mean /= max(float(np.linalg.norm(mean)), 1e-12)
        degrees = np.degrees(2.0 * np.arccos(np.clip(np.abs(aligned @ mean), 0.0, 1.0)))
        gos = float(np.mean(degrees)) if degrees.size else 0.0
        gos_map[grains == grain_id] = gos
        grain_values.append(gos)
        grain_means[grain_id] = mean
    return gos_map, grain_values, grain_means


def _gos_figure(
    ebsd_map: ebsd.Map,
    orientations: Orientation,
    valid: np.ndarray,
    grains: np.ndarray,
) -> Tuple[RawRgbMap, List[float], Dict[int, np.ndarray]]:
    gos_map, grain_values, grain_means = _grain_orientation_stats(orientations, valid, grains)
    valid = grains > 0
    return (
        _raw_rgb_map(_scalar_rgb(gos_map, valid), float(ebsd_map.step_size), "GOS"),
        grain_values,
        grain_means,
    )


def _boundary_rgb(ebsd_map: ebsd.Map, grains: np.ndarray) -> RawRgbMap:
    band = _normalised_band_contrast(ebsd_map)
    rgb = np.repeat(band[..., None], 3, axis=2)
    rgb[_grain_boundaries(grains)] = (0.05, 0.05, 0.05)
    return _raw_rgb_map(rgb, float(ebsd_map.step_size), "GB")


def _family_axis_error(axis: np.ndarray, family: str) -> float:
    direction = np.abs(np.asarray(axis, dtype=float))
    norm = float(np.linalg.norm(direction))
    if norm <= 1e-12:
        return 180.0
    direction /= norm
    if family == "111":
        targets = [np.asarray((1.0, 1.0, 1.0)) / math.sqrt(3.0)]
    elif family == "110":
        targets = [
            np.asarray(value, dtype=float) / math.sqrt(2.0)
            for value in ((1, 1, 0), (1, 0, 1), (0, 1, 1))
        ]
    else:
        targets = [np.asarray(value, dtype=float) for value in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
    best = max(float(np.dot(direction, target)) for target in targets)
    return math.degrees(math.acos(np.clip(best, -1.0, 1.0)))


def _csl_sigma(first: np.ndarray, second: np.ndarray) -> Optional[int]:
    first_w, first_xyz = float(first[0]), first[1:]
    second_w, second_xyz = float(second[0]), second[1:]
    scalar = second_w * first_w + float(np.dot(second_xyz, first_xyz))
    vector = -second_w * first_xyz + first_w * second_xyz - np.cross(second_xyz, first_xyz)
    if scalar < 0:
        scalar = -scalar
        vector = -vector
    angle = math.degrees(2.0 * math.acos(float(np.clip(scalar, -1.0, 1.0))))
    axis = vector / max(float(np.linalg.norm(vector)), 1e-12)
    candidates = (
        (3, 60.00, "111"),
        (5, 36.87, "100"),
        (7, 38.21, "111"),
        (9, 38.94, "110"),
        (11, 50.48, "110"),
    )
    for sigma, ideal_angle, family in candidates:
        tolerance = 15.0 / math.sqrt(sigma)
        if abs(angle - ideal_angle) <= tolerance and _family_axis_error(axis, family) <= tolerance:
            return sigma
    return None


def _csl_figure(
    ebsd_map: ebsd.Map,
    grains: np.ndarray,
    grain_means: Dict[int, np.ndarray],
) -> Tuple[RawRgbMap, Dict[str, float]]:
    band = _normalised_band_contrast(ebsd_map)
    rgb = np.repeat(band[..., None], 3, axis=2)
    pair_pixels: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}
    height, width = grains.shape
    for y in range(height):
        for x in range(width):
            here = int(grains[y, x])
            if here <= 0:
                continue
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if nx >= width or ny >= height:
                    continue
                other = int(grains[ny, nx])
                if other <= 0 or other == here:
                    continue
                pair = tuple(sorted((here, other)))
                pair_pixels.setdefault(pair, []).extend(((y, x), (ny, nx)))

    colours = {
        3: np.asarray((0.95, 0.20, 0.20)),
        5: np.asarray((0.12, 0.72, 0.38)),
        7: np.asarray((0.20, 0.48, 0.95)),
        9: np.asarray((0.68, 0.32, 0.90)),
        11: np.asarray((0.96, 0.58, 0.12)),
    }
    counts: Dict[int, int] = {sigma: 0 for sigma in colours}
    total = 0
    for (first_id, second_id), pixels in pair_pixels.items():
        total += len(pixels)
        try:
            sigma = _csl_sigma(grain_means[first_id], grain_means[second_id])
        except (KeyError, ValueError):
            sigma = None
        colour = colours.get(sigma, np.asarray((0.05, 0.05, 0.05)))
        if sigma is not None:
            counts[sigma] += len(pixels)
        for y, x in pixels:
            rgb[y, x] = colour
    summary = {f"Σ{sigma}": (counts[sigma] / total * 100 if total else 0.0) for sigma in colours}
    return _raw_rgb_map(rgb, float(ebsd_map.step_size), "CSL"), summary


def _orix_phase(ebsd_map: ebsd.Map, family: str) -> Tuple[Any, Phase]:
    source_phase = ebsd_map.primary_phase
    a, b, c, alpha, beta, gamma = source_phase.lattice_params
    lattice = Lattice(
        a=float(a),
        b=float(b),
        c=float(c),
        alpha=math.degrees(float(alpha)),
        beta=math.degrees(float(beta)),
        gamma=math.degrees(float(gamma)),
    )
    point_group = symmetry.D6h if family == "hcp" else symmetry.Oh
    phase = Phase(
        name=str(source_phase.name),
        point_group=point_group,
        structure=Structure(title=str(source_phase.name), lattice=lattice),
    )
    return point_group, phase


def _orientations(ebsd_map: ebsd.Map, point_group: Any) -> Tuple[Orientation, np.ndarray]:
    phase = np.asarray(ebsd_map.data.phase)
    primary_phase = int(ebsd_map.primary_phase_id) + 1
    valid = phase == primary_phase
    euler = np.moveaxis(np.asarray(ebsd_map.data.euler_angle, dtype=float), 0, -1)
    flat = euler[valid]
    if flat.size == 0:
        raise ValueError("EBSD scan has no indexed orientations in its primary phase")
    orientations = Orientation.from_euler(flat, symmetry=point_group, direction="lab2crystal")
    return orientations, valid


def _preferred_family_vectors(family: str, phase: Phase) -> List[np.ndarray]:
    if family == "hcp":
        indices: Sequence[Tuple[int, ...]] = ((0, 0, 0, 1), (1, 0, -1, 0), (1, 1, -2, 0))
        families = [Miller(hkil=value, phase=phase).symmetrise(unique=True) for value in indices]
    else:
        indices = ((1, 0, 0), (1, 1, 0), (1, 1, 1))
        families = [Miller(hkl=value, phase=phase).symmetrise(unique=True) for value in indices]

    result: List[np.ndarray] = []
    for item in families:
        values = np.asarray(item.unit.data, dtype=float).reshape(-1, 3)
        result.append(values)
    return result


def _preferred_orientation_map(
    ebsd_map: ebsd.Map,
    orientations: Orientation,
    valid: np.ndarray,
    family_vectors: Sequence[np.ndarray],
    threshold: float,
    map_name: str,
) -> RawRgbMap:
    crystal_directions = np.asarray((orientations * Vector3d.zvector()).data, dtype=float).reshape(-1, 3)
    scores = np.column_stack(
        [np.max(np.abs(crystal_directions @ vectors.T), axis=1) for vectors in family_vectors]
    )
    selected_family = np.argmax(scores, axis=1)
    selected_angle = np.degrees(np.arccos(np.clip(np.max(scores, axis=1), 0.0, 1.0)))

    band = _normalised_band_contrast(ebsd_map)
    rgb = np.repeat(band[..., None], 3, axis=2)
    valid_indices = np.flatnonzero(valid.reshape(-1))
    accepted = selected_angle <= threshold
    flat_rgb = rgb.reshape(-1, 3)
    alpha = 0.42
    for family_index, color in enumerate(PREFERRED_COLORS):
        pixels = valid_indices[accepted & (selected_family == family_index)]
        flat_rgb[pixels] = flat_rgb[pixels] * (1.0 - alpha) + color * alpha
    return _raw_rgb_map(rgb, float(ebsd_map.step_size), map_name)


def generate_python_scan_figures(
    *,
    crc_content: bytes,
    cpr_content: bytes,
    min_grain_size: int,
    orientation_family: str,
) -> Dict[str, Any]:
    family = "hcp" if str(orientation_family).lower() == "hcp" else "cubic"
    with tempfile.TemporaryDirectory(prefix="ebsd-python-figures-") as temporary_dir:
        base = Path(temporary_dir) / "scan"
        base.with_suffix(".crc").write_bytes(crc_content)
        base.with_suffix(".cpr").write_bytes(cpr_content)
        ebsd_map = ebsd.Map(str(base), data_type="OxfordBinary")

        point_group, phase = _orix_phase(ebsd_map, family)
        orientations, valid = _orientations(ebsd_map, point_group)
        family_vectors = _preferred_family_vectors(family, phase)
        grain_map, grains = _grain_map(ebsd_map, min_grain_size)
        grain_boundaries = _boundary_rgb(ebsd_map, grains)
        kam_map, kam_values = _kam_figure(ebsd_map, valid)
        gos_map, gos_values, grain_means = _gos_figure(ebsd_map, orientations, valid, grains)
        csl_map, csl_summary = _csl_figure(ebsd_map, grains, grain_means)
        return {
            "grain_map": grain_map,
            "grain_boundaries": grain_boundaries,
            "kam_map": kam_map,
            "kam_values": kam_values,
            "gos_map": gos_map,
            "gos_values": gos_values,
            "csl_map": csl_map,
            "csl_summary": csl_summary,
            "preferred_20": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 20.0, "POR20"
            ),
            "preferred_15": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 15.0, "POR15"
            ),
        }
