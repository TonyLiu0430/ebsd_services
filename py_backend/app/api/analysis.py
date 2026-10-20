"""Feature analysis endpoint."""

from fastapi import APIRouter

from app.schemas import AnalysisRequest
from app.analysis import service as analysis_service


router = APIRouter()


@router.post("/analysis")
def analysis(req: AnalysisRequest):
    return analysis_service.analysis(req)
