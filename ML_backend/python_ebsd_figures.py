from __future__ import annotations

import colorsys
import io
import math
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np
from defdap import ebsd
from diffpy.structure import Lattice, Structure
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
from orix.crystal_map import Phase
from orix.measure import pole_density_function
from orix.plot import InversePoleFigurePlot, StereographicPlot  # noqa: F401
from orix.quaternion import Orientation, symmetry
from orix.vector import Miller, Vector3d


MAX_TEXTURE_ORIENTATIONS = 60_000
TEXTURE_GRID_RESOLUTION_DEG = 1.0
TEXTURE_HALF_WIDTH_DEG = 10.0
TEXTURE_MIN_MRD = 0.1
TEXTURE_CMAP = "jet"
PREFERRED_COLORS = np.asarray(
    (
        (0.95, 0.36, 0.36),
        (0.35, 0.82, 0.39),
        (0.40, 0.45, 0.90),
    ),
    dtype=float,
)


def _png(figure: Figure) -> bytes:
    output = io.BytesIO()
    FigureCanvasAgg(figure).print_png(output)
    return output.getvalue()


def _nice_scale_length(maximum: float) -> float:
    if not math.isfinite(maximum) or maximum <= 0:
        return 0.0
    exponent = 10 ** math.floor(math.log10(maximum))
    normalized = maximum / exponent
    multiplier = 5 if normalized >= 5 else 2 if normalized >= 2 else 1
    return multiplier * exponent


def _image_figure(rgb: np.ndarray, step_size: float) -> bytes:
    height, width = rgb.shape[:2]
    figure = Figure(figsize=(4.2, 2.45), dpi=180, facecolor="white")
    axis = figure.add_axes((0.01, 0.05, 0.98, 0.94))
    axis.imshow(rgb, interpolation="nearest")
    axis.set_xlim(-0.5, width - 0.5)
    axis.set_ylim(height - 0.5, -0.5)
    axis.axis("off")

    length_um = _nice_scale_length(width * step_size * 0.28)
    if length_um > 0 and step_size > 0:
        bar_pixels = min(width * 0.6, length_um / step_size)
        center = (width - 1) / 2
        y = height * 0.93
        axis.plot(
            (center - bar_pixels / 2, center + bar_pixels / 2),
            (y, y),
            color="black",
            linewidth=2.2,
            solid_capstyle="butt",
        )
        label = f"{length_um:g} um"
        axis.text(center, y - height * 0.035, label, ha="center", va="bottom", fontsize=5, color="black")
    return _png(figure)


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


def _grain_map(ebsd_map: ebsd.Map, min_grain_size: int) -> Tuple[bytes, np.ndarray]:
    # Access through Datastore so DefDAP stores the generated boundary set
    # before find_grains() consumes it.
    _ = ebsd_map.data.grain_boundaries
    # DefDAP indexes every grain first. Filtering here avoids its empty-list
    # edge case when every grain is smaller than the report threshold.
    grains = np.asarray(ebsd_map.find_grains(min_grain_size=1), dtype=int)
    grain_ids, grain_sizes = np.unique(grains[grains > 0], return_counts=True)
    for grain_id in grain_ids[grain_sizes < max(1, min_grain_size)]:
        grains[grains == grain_id] = -2
    rgb = np.full(grains.shape + (3,), 0.93, dtype=float)
    for grain_id in np.unique(grains):
        if grain_id <= 0:
            continue
        hue = (grain_id * 0.61803398875) % 1.0
        rgb[grains == grain_id] = colorsys.hsv_to_rgb(hue, 0.78, 0.98)
    rgb[grains <= 0] = 0.0
    rgb[_grain_boundaries(grains)] = 0.05
    return _image_figure(rgb, float(ebsd_map.step_size)), grains


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


def _texture_subset(orientations: Orientation) -> Orientation:
    count = orientations.size
    if count <= MAX_TEXTURE_ORIENTATIONS:
        return orientations
    indices = np.linspace(0, count - 1, MAX_TEXTURE_ORIENTATIONS, dtype=int)
    return orientations[indices]


def _families(family: str, phase: Phase) -> Tuple[List[Miller], List[str]]:
    if family == "hcp":
        indices: Sequence[Tuple[int, ...]] = ((0, 0, 0, 1), (1, 0, -1, 0), (1, 1, -2, 0))
        labels = [r"$(0001)$", r"$(10\bar{1}0)$", r"$(11\bar{2}0)$"]
        return [Miller(hkil=value, phase=phase).symmetrise(unique=True) for value in indices], labels
    indices = ((1, 0, 0), (1, 1, 0), (1, 1, 1))
    labels = [r"$(100)$", r"$(110)$", r"$(111)$"]
    return [Miller(hkl=value, phase=phase).symmetrise(unique=True) for value in indices], labels


def _common_density_limits(vectors: Sequence[Vector3d], point_group: Any = None) -> Tuple[float, float]:
    maxima: List[float] = []
    for values in vectors:
        density, _ = pole_density_function(
            values,
            resolution=TEXTURE_GRID_RESOLUTION_DEG,
            sigma=TEXTURE_HALF_WIDTH_DEG,
            hemisphere="upper",
            symmetry=point_group,
            log=False,
            mrd=True,
        )
        finite = np.asarray(density.compressed(), dtype=float)
        if finite.size:
            maxima.append(float(np.max(finite)))
    return TEXTURE_MIN_MRD, max(TEXTURE_MIN_MRD, max(maxima, default=TEXTURE_MIN_MRD))


def _pole_figure(orientations: Orientation, family: str, phase: Phase) -> bytes:
    orientation_subset = _texture_subset(orientations)
    families, labels = _families(family, phase)
    poles = [orientation_subset.inv().outer(item, lazy=False, progressbar=False) for item in families]
    vmin, vmax = _common_density_limits(poles)

    figure = Figure(figsize=(7.0, 2.35), dpi=180, facecolor="white")
    axes = [figure.add_subplot(1, 3, index + 1, projection="stereographic", hemisphere="upper") for index in range(3)]
    meshes = []
    for axis, values, label in zip(axes, poles, labels):
        axis.pole_density_function(
            values,
            resolution=TEXTURE_GRID_RESOLUTION_DEG,
            sigma=TEXTURE_HALF_WIDTH_DEG,
            log=False,
            colorbar=False,
            cmap=TEXTURE_CMAP,
            vmin=vmin,
            vmax=vmax,
        )
        axis.set_title(label, fontsize=8, pad=2)
        axis.set_labels("X", "Y", None)
        for text in axis.texts:
            text.set_fontsize(7)
        if axis.collections:
            meshes.append(axis.collections[-1])
    figure.subplots_adjust(left=0.02, right=0.91, bottom=0.03, top=0.96, wspace=0.20)
    if meshes:
        colorbar = figure.colorbar(meshes[-1], ax=axes, fraction=0.025, pad=0.03)
        colorbar.ax.tick_params(labelsize=5)
        colorbar.set_label("MRD", fontsize=6)
    return _png(figure)


def _inverse_pole_figure(orientations: Orientation, point_group: Any) -> bytes:
    orientation_subset = _texture_subset(orientations)
    directions = (Vector3d.xvector(), Vector3d.yvector(), Vector3d.zvector())
    vectors = [orientation_subset * direction for direction in directions]
    vmin, vmax = _common_density_limits(vectors, point_group)
    figure = Figure(figsize=(7.0, 2.35), dpi=180, facecolor="white")
    axes = [
        figure.add_subplot(1, 3, index + 1, projection="ipf", symmetry=point_group, direction=direction)
        for index, direction in enumerate(directions)
    ]
    meshes = []
    for axis, values, label in zip(axes, vectors, ("X", "Y", "Z")):
        axis.pole_density_function(
            values,
            resolution=TEXTURE_GRID_RESOLUTION_DEG,
            sigma=TEXTURE_HALF_WIDTH_DEG,
            log=False,
            colorbar=False,
            cmap=TEXTURE_CMAP,
            vmin=vmin,
            vmax=vmax,
        )
        axis.set_title(label, fontsize=8, pad=2)
        for text in axis.texts:
            text.set_fontsize(7)
        if axis.collections:
            meshes.append(axis.collections[-1])
    figure.subplots_adjust(left=0.06, right=0.91, bottom=0.06, top=0.94, wspace=0.28)
    if meshes:
        colorbar = figure.colorbar(meshes[-1], ax=axes, fraction=0.025, pad=0.03)
        colorbar.ax.tick_params(labelsize=5)
        colorbar.set_label("MRD", fontsize=6)
    return _png(figure)


def _preferred_family_vectors(family: str, phase: Phase) -> List[np.ndarray]:
    families, _ = _families(family, phase)
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
) -> bytes:
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
    return _image_figure(rgb, float(ebsd_map.step_size))


def generate_python_scan_figures(
    *,
    crc_content: bytes,
    cpr_content: bytes,
    min_grain_size: int,
    orientation_family: str,
) -> Dict[str, bytes]:
    family = "hcp" if str(orientation_family).lower() == "hcp" else "cubic"
    with tempfile.TemporaryDirectory(prefix="ebsd-python-figures-") as temporary_dir:
        base = Path(temporary_dir) / "scan"
        base.with_suffix(".crc").write_bytes(crc_content)
        base.with_suffix(".cpr").write_bytes(cpr_content)
        ebsd_map = ebsd.Map(str(base), data_type="OxfordBinary")

        point_group, phase = _orix_phase(ebsd_map, family)
        orientations, valid = _orientations(ebsd_map, point_group)
        family_vectors = _preferred_family_vectors(family, phase)
        grain_map, _ = _grain_map(ebsd_map, min_grain_size)
        return {
            "grain_map": grain_map,
            "preferred_20": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 20.0
            ),
            "preferred_15": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 15.0
            ),
            "pole_figure": _pole_figure(orientations, family, phase),
            "inverse_pole_figure": _inverse_pole_figure(orientations, point_group),
        }
