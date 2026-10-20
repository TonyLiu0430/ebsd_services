"""Report data and PowerPoint endpoints."""

from fastapi import APIRouter, Depends, Request
from sqlalchemy.orm import Session

from app.db.store import get_db
from app.schemas import ReportPptRequest
from app.services import reports


router = APIRouter()


@router.post("/reports/data")
def generate_report_data(
    req: ReportPptRequest,
    request: Request,
    db: Session = Depends(get_db),
):
    return reports.generate_report_data(req, request, db)


@router.post("/reports/pptx", status_code=202)
def create_ppt_report_job(req: ReportPptRequest, request: Request):
    return reports.create_ppt_report_job(req, request)


@router.get("/reports/pptx/{job_id}")
def get_ppt_report_job(job_id: str, request: Request):
    return reports.get_ppt_report_job(job_id, request)


@router.get("/reports/pptx/{job_id}/download")
def download_ppt_report_job(job_id: str, request: Request):
    return reports.download_ppt_report_job(job_id, request)
