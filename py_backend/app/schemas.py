"""Request models shared by API routes and backend services."""

from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field  # type: ignore


class ReportPptRequest(BaseModel):
    sample: str
    golden: Optional[str] = None
    version_key: Optional[str] = None
    min_grain_size: Optional[int] = None
    ipf_reference_vector: Optional[List[float]] = None
    ipf_denoindex_threshold: Optional[float] = None
    ipf_denoindex_misorientation_threshold: Optional[float] = None
    hist_bin_start_mode: str = "zero"
    hist_bin_width_mode: str = "absolute"
    hist_bin_width_ratio: float = 5.0
    hist_bin_width_absolute: float = 10.0
    grain_chart_mode: str = "hist"
    histogram_y_mode: str = "ratio"
    orientation_line_y_max: Optional[float] = None
    show_microstructure_page: bool = False
    show_triangle_labels: bool = True
    show_orientation_line_labels: bool = True
    ipf_display_target: str = "analysis"
    grid_display_target: str = "analysis"
    selected_features: List[str] = Field(default_factory=list)
    color_thresholds: List[float] = Field(default_factory=lambda: [30.0, 70.0])


class AnalysisRequest(BaseModel):
    features: List[str]
    golden: str
    data: Dict[str, Dict[str, Dict[str, Any]]]
