"""Feature comparison and statistical analysis workflows."""

import statistics
from typing import Any, Callable, Dict, List, Optional

from fastapi import HTTPException

from app.analysis.statistics import (
    Overall_distribution_analysis, max_compare, mean_compare, numeric_compare,
    p25_compare, p75_compare, sd_compare,
)
from app.schemas import AnalysisRequest


GRAIN_FEATURES: Dict[str, Callable[[List[float], List[float]], float]] = {
    "overall distribution : wasserstein_distance / mean": Overall_distribution_analysis,
    "mean": mean_compare,
    "max": max_compare,
    "25 percentiles": p25_compare,
    "75 percentiles": p75_compare,
    "standard deviation": sd_compare
}

ORIENTATION_FEATURES: Dict[str, Callable[[float, float], float]] = {
    "[001] (deviation 20%)": numeric_compare,
    "[011] (deviation 20%)": numeric_compare,
    "[111] (deviation 20%)": numeric_compare,
    "[0001] (deviation 20%)": numeric_compare,
    "[10-10] (deviation 20%)": numeric_compare,
    "[11-20] (deviation 20%)": numeric_compare,
    "[001] (deviation 15%)": numeric_compare,
    "[011] (deviation 15%)": numeric_compare,
    "[111] (deviation 15%)": numeric_compare,
    "[0001] (deviation 15%)": numeric_compare,
    "[10-10] (deviation 15%)": numeric_compare,
    "[11-20] (deviation 15%)": numeric_compare,
}


def orientation_label_key(label: Any) -> str:
    return str(label).strip().strip("[]").replace("−", "-")


def orientation_index_for_feature(feature: str, feature_payload: Dict[str, Any]) -> Optional[int]:
    axis = orientation_label_key(feature.split(']')[0].lstrip('['))
    labels = feature_payload.get("orientation_labels")
    if isinstance(labels, list):
        for idx, label in enumerate(labels):
            if orientation_label_key(label) == axis:
                return idx
    default_idx_map = {"001": 0, "011": 1, "111": 2}
    return default_idx_map.get(axis)

MATERIAL_PHASES = [
    {"key": "Cu-FCC", "element": "Cu", "phase": "FCC", "space_group": 225, "a": 3.615},
    {"key": "Al-FCC", "element": "Al", "phase": "FCC", "space_group": 225, "a": 4.050},
    {"key": "Ni-FCC", "element": "Ni", "phase": "FCC", "space_group": 225, "a": 3.524},
    {"key": "Ag-FCC", "element": "Ag", "phase": "FCC", "space_group": 225, "a": 4.086},
    {"key": "Au-FCC", "element": "Au", "phase": "FCC", "space_group": 225, "a": 4.078},
    {"key": "Pt-FCC", "element": "Pt", "phase": "FCC", "space_group": 225, "a": 3.924},
    {"key": "Pd-FCC", "element": "Pd", "phase": "FCC", "space_group": 225, "a": 3.889},
    {"key": "Fe-BCC", "element": "Fe", "phase": "BCC", "space_group": 229, "a": 2.866},
    {"key": "Fe-FCC", "element": "Fe", "phase": "FCC", "space_group": 225, "a": 3.646},
    {"key": "W-BCC", "element": "W", "phase": "BCC", "space_group": 229, "a": 3.165},
    {"key": "Mo-BCC", "element": "Mo", "phase": "BCC", "space_group": 229, "a": 3.147},
    {"key": "Cr-BCC", "element": "Cr", "phase": "BCC", "space_group": 229, "a": 2.884},
    {"key": "Ta-BCC", "element": "Ta", "phase": "BCC", "space_group": 229, "a": 3.305},
    {"key": "Nb-BCC", "element": "Nb", "phase": "BCC", "space_group": 229, "a": 3.300},
    {"key": "Ti-HCP", "element": "Ti", "phase": "HCP", "space_group": 194, "a": 2.951, "c": 4.684},
    {"key": "Mg-HCP", "element": "Mg", "phase": "HCP", "space_group": 194, "a": 3.209, "c": 5.211},
    {"key": "Zn-HCP", "element": "Zn", "phase": "HCP", "space_group": 194, "a": 2.665, "c": 4.947},
    {"key": "Co-HCP", "element": "Co", "phase": "HCP", "space_group": 194, "a": 2.507, "c": 4.069},
    {"key": "Si-Diamond", "element": "Si", "phase": "Diamond", "space_group": 227, "a": 5.431},
]


def analysis(req: AnalysisRequest):
    return compute_analysis(req.features, req.golden, req.data)


def compute_analysis(
    features: List[str],
    golden: str,
    data: Dict[str, Dict[str, Dict[str, Any]]],
) -> Dict[str, Dict[str, float]]:
    for feat in features:
        if feat not in GRAIN_FEATURES and feat not in ORIENTATION_FEATURES:
            raise HTTPException(status_code=400, detail="Invalid feature options")
    if golden not in data:
        raise HTTPException(status_code=400, detail="Invalid golden sample")
    res: Dict[str, Dict[str, float]] = {}
    for (sample, posdata) in data.items():
        if sample == golden:
            continue
        for (pos, cur_data) in posdata.items():
            golden_data = data[golden].get(pos)
            if not golden_data:
                continue

            statRes: List[float] = []
            for feat in features:
                if feat in GRAIN_FEATURES:
                    if "grains" not in golden_data or "grains" not in cur_data:
                        continue
                    handler = GRAIN_FEATURES[feat]
                    statRes.append(handler(golden_data["grains"], cur_data["grains"]))
                elif feat in ORIENTATION_FEATURES:
                    handler = ORIENTATION_FEATURES[feat]
                    # feat example: "[001] (deviation 20%)" or "[0001] (deviation 20%)"
                    try:
                        deviation = "20" if "20%" in feat else "15"
                        ratio_key = f"orientation_ratio({deviation}%)"
                        idx = orientation_index_for_feature(feat, cur_data)
                        golden_idx = orientation_index_for_feature(feat, golden_data)
                        if idx is None or golden_idx is None or idx != golden_idx:
                            continue
                        golden_ratio = golden_data[ratio_key]
                        sample_ratio = cur_data[ratio_key]
                        statRes.append(handler(float(golden_ratio[idx]), float(sample_ratio[idx])))
                    except (KeyError, IndexError, TypeError, ValueError):
                        continue

            # TODO 改用 Winsorized mean 並且前端要可調整權重
            if not statRes:
                continue
            if len(statRes) > 1:
                statRes = [abs(x) for x in statRes]
            avg = statistics.mean(statRes)
            res.setdefault(sample, {})[pos] = avg
    return res
