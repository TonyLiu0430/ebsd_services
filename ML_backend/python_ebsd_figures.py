from __future__ import annotations

import io
import math
import tempfile
from dataclasses import dataclass
from functools import lru_cache
from itertools import permutations, product
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np
from defdap import ebsd
from diffpy.structure import Lattice, Structure
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.figure import Figure
from matplotlib.patches import Circle
from orix.crystal_map import Phase
from orix.measure import pole_density_function
from orix.plot import InversePoleFigurePlot, StereographicPlot  # noqa: F401
from orix.quaternion import Orientation, symmetry
from orix.sampling import sample_S2
from orix.vector import Miller, Vector3d
from scipy.spatial import cKDTree


MAX_TEXTURE_ORIENTATIONS = 60_000
TEXTURE_GRID_RESOLUTION_DEG = 1.0
TEXTURE_HALF_WIDTH_DEG = 10.0
TEXTURE_FWHM_CALIBRATION = 1.025
IPF_FWHM_CALIBRATION = 1.085
TEXTURE_SPHERE_SAMPLING_DEG = 2.0
TEXTURE_MIN_MRD = 0.1
TEXTURE_CMAP = "jet"
TEXTURE_DENSITY_GRID_SIZE = 251
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


def _png(figure: Figure) -> bytes:
    output = io.BytesIO()
    FigureCanvasAgg(figure).print_png(output)
    return output.getvalue()


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
        labels = [r"$\{0001\}$", r"$\{10\bar{1}0\}$", r"$\{11\bar{2}0\}$"]
        return [Miller(hkil=value, phase=phase).symmetrise(unique=True) for value in indices], labels
    indices = ((1, 0, 0), (1, 1, 0), (1, 1, 1))
    labels = [r"$\{100\}$", r"$\{110\}$", r"$\{111\}$"]
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


def _channel5_plot_coordinates(vectors: Vector3d) -> np.ndarray:
    """Convert sample coordinates to Channel 5's PF display coordinates."""
    source_xyz = np.asarray(vectors.unit.data, dtype=float).reshape(-1, 3)
    return np.column_stack((-source_xyz[:, 0], -source_xyz[:, 2], source_xyz[:, 1]))


def _lambert_equal_area_density(vectors: Vector3d) -> np.ma.MaskedArray:
    """Estimate Channel 5-like upper-hemisphere MRD on the orientation sphere.

    The reference Channel 5 plot uses -X to the right, -Z to the top and +Y as
    its projection axis. Its 10 degree half-width setting matches a 10 degree
    Gaussian FWHM for this scan. Doing the smoothing on the sphere is crucial:
    azimuth is undefined at the pole, so filtering an azimuth/polar histogram
    creates false radial convergence at the disk centre.
    """
    xyz = _channel5_plot_coordinates(vectors)
    xyz[xyz[:, 2] < 0] *= -1

    cluster_vectors = np.asarray(
        sample_S2(resolution=TEXTURE_SPHERE_SAMPLING_DEG, method="equal_area").data,
        dtype=float,
    )
    cluster_vectors = cluster_vectors[cluster_vectors[:, 2] >= -1e-10]
    _, nearest = cKDTree(cluster_vectors).query(xyz, workers=-1)
    weights = np.bincount(nearest, minlength=cluster_vectors.shape[0]).astype(float)
    occupied = weights > 0
    cluster_vectors = cluster_vectors[occupied]
    weights = weights[occupied]

    grid_size = TEXTURE_DENSITY_GRID_SIZE
    centers = np.linspace(-1.0 + 1.0 / grid_size, 1.0 - 1.0 / grid_size, grid_size)
    xx, yy = np.meshgrid(centers, centers)
    radius_squared = xx * xx + yy * yy
    disk = radius_squared <= 1.0

    # Inverse normalized Lambert azimuthal equal-area projection. The disk rim
    # is the equator and its centre is the sample +Y direction.
    radius = np.sqrt(np.maximum(radius_squared[disk], 0.0))
    z = 1.0 - radius_squared[disk]
    equatorial = np.sqrt(np.maximum(1.0 - z * z, 0.0))
    query_x = np.divide(
        xx[disk] * equatorial,
        radius,
        out=np.zeros_like(radius),
        where=radius > 1e-12,
    )
    query_y = np.divide(
        yy[disk] * equatorial,
        radius,
        out=np.zeros_like(radius),
        where=radius > 1e-12,
    )
    query_vectors = np.column_stack((query_x, query_y, z))

    # FWHM = 2 sigma sqrt(2 ln 2). Four sigma retains all but 0.034% of
    # the Gaussian peak while keeping the spherical calculation inexpensive.
    effective_fwhm = TEXTURE_HALF_WIDTH_DEG * TEXTURE_FWHM_CALIBRATION
    sigma = math.radians(effective_fwhm) / (
        2.0 * math.sqrt(2.0 * math.log(2.0))
    )
    cutoff_angle = 4.0 * sigma
    cutoff_chord = 2.0 * math.sin(cutoff_angle / 2.0)
    positive_tree = cKDTree(cluster_vectors)
    negative_tree = cKDTree(-cluster_vectors)
    nearby = positive_tree.query_ball_point(query_vectors, cutoff_chord, workers=-1)
    nearby_antipodal = negative_tree.query_ball_point(
        query_vectors, cutoff_chord, workers=-1
    )

    values = np.empty(query_vectors.shape[0], dtype=float)
    for index, direction in enumerate(query_vectors):
        candidates = np.asarray(nearby[index] + nearby_antipodal[index], dtype=int)
        dots = np.clip(np.abs(cluster_vectors[candidates] @ direction), 0.0, 1.0)
        angles = np.arccos(dots)
        values[index] = np.sum(
            np.exp(-0.5 * (angles / sigma) ** 2) * weights[candidates]
        )

    mean_density = float(np.mean(values))
    if mean_density > 0:
        values /= mean_density
    density = np.zeros_like(xx)
    density[disk] = values
    return np.ma.array(density, mask=~disk)


# Legacy renderers are retained only as the cross-language test oracle in
# test_texture_cpp_parity.py. Production reports request both figures from C++.
def _pole_figure(orientations: Orientation, family: str, phase: Phase) -> bytes:
    orientation_subset = _texture_subset(orientations)
    families, labels = _families(family, phase)
    poles = [orientation_subset.inv().outer(item, lazy=False, progressbar=False) for item in families]
    densities = [_lambert_equal_area_density(item) for item in poles]
    vmin = TEXTURE_MIN_MRD
    vmax = max(vmin, max(float(np.max(item)) for item in densities))

    figure = Figure(figsize=(7.0, 2.35), dpi=180, facecolor="white")
    axes = [figure.add_subplot(1, 3, index + 1) for index in range(3)]
    meshes = []
    for axis, density, label in zip(axes, densities, labels):
        mesh = axis.imshow(
            density,
            origin="lower",
            extent=(-1, 1, -1, 1),
            interpolation="bicubic",
            cmap=CHANNEL5_DENSITY_CMAP,
            vmin=vmin,
            vmax=vmax,
        )
        axis.add_patch(Circle((0, 0), 1, fill=False, edgecolor="black", linewidth=1.0))
        axis.plot((-1, 1), (0, 0), color="black", linewidth=0.45, alpha=0.8)
        axis.plot((0, 0), (-1, 1), color="black", linewidth=0.45, alpha=0.8)
        axis.set_title(label, fontsize=8, pad=2)
        label_box = {"facecolor": "white", "edgecolor": "none", "pad": 0.5}
        axis.text(0.96, 0, "X0", ha="left", va="center", fontsize=7, bbox=label_box)
        axis.text(0, 0.96, "Z0", ha="center", va="bottom", fontsize=7, bbox=label_box)
        axis.set_xlim(-1.05, 1.05)
        axis.set_ylim(-1.05, 1.05)
        axis.set_aspect("equal")
        axis.axis("off")
        meshes.append(mesh)
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

    if point_group == symmetry.Oh:
        return _channel5_cubic_inverse_pole_figure(vectors)

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


@lru_cache(maxsize=1)
def _channel5_cubic_ipf_grid() -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Return an equal-area cubic IPF grid in Channel 5's display layout."""
    grid_size = TEXTURE_DENSITY_GRID_SIZE
    right = math.sqrt((1.0 - 1.0 / math.sqrt(3.0)) / 2.0)
    down = math.sqrt(1.0 - 1.0 / math.sqrt(2.0))
    plot_x = np.linspace(-0.015, right + 0.015, grid_size)
    plot_y = np.linspace(-down - 0.015, 0.015, grid_size)
    xx, yy = np.meshgrid(plot_x, plot_y)

    # Undo the clockwise rotation used by Channel 5, then invert the
    # normalized Lambert azimuthal equal-area projection.
    projected_x = -yy
    projected_y = xx
    radius_squared = projected_x * projected_x + projected_y * projected_y
    z = 1.0 - radius_squared
    equatorial = np.sqrt(np.maximum(1.0 - z * z, 0.0))
    radius = np.sqrt(np.maximum(radius_squared, 0.0))
    x = np.divide(
        projected_x * equatorial,
        radius,
        out=np.zeros_like(radius),
        where=radius > 1e-12,
    )
    y = np.divide(
        projected_y * equatorial,
        radius,
        out=np.zeros_like(radius),
        where=radius > 1e-12,
    )
    query_vectors = np.column_stack((x.ravel(), y.ravel(), z.ravel()))
    inside = (
        (y >= -1e-10)
        & (x >= y - 1e-10)
        & (z >= x - 1e-10)
        & (radius_squared <= 1.0)
    ).ravel()
    return xx, yy, query_vectors, inside


def _channel5_cubic_ipf_density(vectors: Vector3d) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Calculate cubic IPF MRD with symmetry-safe spherical smoothing."""
    raw = np.abs(np.asarray(vectors.unit.data, dtype=float).reshape(-1, 3))
    sphere_vectors = np.asarray(
        sample_S2(resolution=TEXTURE_SPHERE_SAMPLING_DEG, method="equal_area").data,
        dtype=float,
    )
    sphere_vectors = sphere_vectors[sphere_vectors[:, 2] >= -1e-10]
    sphere_tree = cKDTree(sphere_vectors)
    weights = np.zeros(sphere_vectors.shape[0], dtype=float)

    # Cubic m-3m has 24 unique axis directions. Expanding the input before
    # smoothing mirrors the density correctly across all fundamental-sector
    # boundaries, including the [001]-[101] edge.
    xy_signs = tuple(product((-1.0, 1.0), repeat=2))
    for permutation in permutations(range(3)):
        permuted = raw[:, permutation]
        for sign_x, sign_y in xy_signs:
            signed = permuted * np.asarray((sign_x, sign_y, 1.0))
            _, nearest = sphere_tree.query(signed, workers=-1)
            weights += np.bincount(nearest, minlength=sphere_vectors.shape[0])

    occupied = weights > 0
    sphere_vectors = sphere_vectors[occupied]
    weights = weights[occupied]
    xx, yy, query_vectors, inside = _channel5_cubic_ipf_grid()
    queries = query_vectors[inside]

    effective_fwhm = TEXTURE_HALF_WIDTH_DEG * IPF_FWHM_CALIBRATION
    sigma = math.radians(effective_fwhm) / (
        2.0 * math.sqrt(2.0 * math.log(2.0))
    )
    cutoff_chord = 2.0 * math.sin(4.0 * sigma / 2.0)
    nearby = cKDTree(sphere_vectors).query_ball_point(
        queries, cutoff_chord, workers=-1
    )
    values = np.empty(queries.shape[0], dtype=float)
    for index, direction in enumerate(queries):
        candidates = np.asarray(nearby[index], dtype=int)
        angles = np.arccos(
            np.clip(sphere_vectors[candidates] @ direction, -1.0, 1.0)
        )
        values[index] = np.sum(
            np.exp(-0.5 * (angles / sigma) ** 2) * weights[candidates]
        )
    mean_density = float(np.mean(values))
    if mean_density > 0:
        values /= mean_density

    density = np.full(xx.shape, np.nan)
    density.ravel()[inside] = values
    return xx, yy, density


def _channel5_cubic_inverse_pole_figure(vectors: Sequence[Vector3d]) -> bytes:
    densities = [_channel5_cubic_ipf_density(values) for values in vectors]
    vmin = min(float(np.nanmin(density)) for _, _, density in densities)
    vmax = max(float(np.nanmax(density)) for _, _, density in densities)
    right = math.sqrt((1.0 - 1.0 / math.sqrt(3.0)) / 2.0)
    down = math.sqrt(1.0 - 1.0 / math.sqrt(2.0))

    figure = Figure(figsize=(7.0, 2.35), dpi=180, facecolor="white")
    axes = [figure.add_subplot(1, 3, index + 1) for index in range(3)]
    meshes = []
    for axis, label, (xx, yy, density) in zip(axes, ("X0", "Y0", "Z0"), densities):
        mesh = axis.contourf(
            xx,
            yy,
            density,
            levels=np.linspace(vmin, vmax, 32),
            cmap=CHANNEL5_DENSITY_CMAP,
            vmin=vmin,
            vmax=vmax,
        )
        axis.plot((0, 0), (0, -down), color="black", linewidth=0.7)
        axis.plot((0, right), (0, -right), color="black", linewidth=0.7)
        boundary_parameter = np.linspace(0.0, 1.0, 100)
        boundary_vectors = np.column_stack(
            (
                np.ones_like(boundary_parameter),
                boundary_parameter,
                np.ones_like(boundary_parameter),
            )
        )
        boundary_vectors /= np.linalg.norm(boundary_vectors, axis=1)[:, None]
        boundary_radius = np.sqrt(1.0 - boundary_vectors[:, 2])
        boundary_equatorial = np.linalg.norm(boundary_vectors[:, :2], axis=1)
        boundary_projected_x = boundary_radius * boundary_vectors[:, 0] / boundary_equatorial
        boundary_projected_y = boundary_radius * boundary_vectors[:, 1] / boundary_equatorial
        axis.plot(
            boundary_projected_y,
            -boundary_projected_x,
            color="black",
            linewidth=0.7,
        )
        axis.set_title(label, fontsize=8, pad=2)
        axis.text(0, 0.012, "001", ha="center", va="bottom", fontsize=7)
        axis.text(0, -down - 0.012, "101", ha="center", va="top", fontsize=7)
        axis.text(right + 0.008, -right, "111", ha="left", va="center", fontsize=7)
        axis.set_aspect("equal")
        axis.axis("off")
        meshes.append(mesh)
    figure.subplots_adjust(left=0.02, right=0.91, bottom=0.04, top=0.93, wspace=0.20)
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
        grain_map, _ = _grain_map(ebsd_map, min_grain_size)
        return {
            "grain_map": grain_map,
            "preferred_20": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 20.0, "POR20"
            ),
            "preferred_15": _preferred_orientation_map(
                ebsd_map, orientations, valid, family_vectors, 15.0, "POR15"
            ),
        }
