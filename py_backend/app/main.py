"""FastAPI application composition."""

from fastapi import FastAPI

from app.api.auth import router as auth_router
from app.api.analysis import router as analysis_router
from app.api.ebsd import router as ebsd_router
from app.api.reports import router as reports_router
from app.db.store import init_db
from app.services import reports
from app.services.auth import validate_auth_config


app = FastAPI()
app.include_router(auth_router)
app.include_router(ebsd_router)
app.include_router(reports_router)
app.include_router(analysis_router)


@app.on_event("startup")
def startup() -> None:
    validate_auth_config()
    init_db()
    reports.clear_ppt_job_storage()
    reports.start_ppt_job_cleanup()


@app.on_event("shutdown")
def shutdown() -> None:
    reports.PPT_JOB_CLEANUP_STOP.set()
