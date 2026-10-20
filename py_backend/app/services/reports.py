"""Report context generation and asynchronous PowerPoint jobs."""

import math
import os
import pathlib
import shutil
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Dict, List, Optional

from fastapi import Depends, HTTPException, Request
from fastapi.responses import FileResponse
from sqlalchemy import select
from sqlalchemy.orm import Session
from starlette.background import BackgroundTask

from app.analysis.figures import RawRgbMap, generate_python_scan_figures
from app.analysis.service import compute_analysis
from app.db.store import SessionLocal, UserEbsdPair, get_db
from app.integrations.cpp import (
    cpp_band_contrast_map, cpp_features, cpp_inverse_pole_figure, cpp_ipf_legend,
    cpp_ipf_map, cpp_pair_ipf_legend, cpp_pole_figure, cpp_scale_bar,
)
from app.reports.presentation import build_pptx_report
from app.schemas import ReportPptRequest
from app.services.auth import current_user
from app.services.ebsd import (
    normalize_reference_vector,
    report_ipf_vectors, resolve_retest_version, validate_ipf_denoindex_misorientation_threshold,
    validate_ipf_denoindex_threshold, validate_min_grain_size,
)


PPT_JOB_DIR = pathlib.Path(os.getenv("PPT_JOB_DIR", "/tmp/ebsd-ppt-jobs"))
PPT_JOB_TTL_SECONDS = int(os.getenv("PPT_JOB_TTL_SECONDS", "1800"))
PPT_JOB_CLEANUP_INTERVAL_SECONDS = int(os.getenv("PPT_JOB_CLEANUP_INTERVAL_SECONDS", "60"))
PPTX_MEDIA_TYPE = "application/vnd.openxmlformats-officedocument.presentationml.presentation"


@dataclass
class PptJob:
    id: str
    user_id: str
    created_at: float
    status: str = "queued"
    updated_at: float = 0.0
    filename: Optional[str] = None
    file_path: Optional[pathlib.Path] = None
    error: Optional[str] = None


PPT_JOBS: Dict[str, PptJob] = {}
PPT_JOBS_LOCK = threading.RLock()
PPT_JOB_EXECUTOR = ThreadPoolExecutor(max_workers=1, thread_name_prefix="ppt-report")
PPT_JOB_CLEANUP_STOP = threading.Event()
PPT_JOB_CLEANUP_THREAD: Optional[threading.Thread] = None


def ppt_job_payload(job: PptJob) -> Dict[str, Any]:
    payload: Dict[str, Any] = {"id": job.id, "status": job.status}
    if job.status == "failed" and job.error:
        payload["error"] = job.error
    if job.status == "completed":
        # Caddy exposes the ML backend under /api and strips that prefix before
        # proxying the request to FastAPI. Return the public browser URL here,
        # not the backend-internal route.
        payload["download_url"] = f"/api/reports/pptx/{job.id}/download"
    return payload


def cleanup_ppt_jobs() -> None:
    """Remove expired report files and their in-memory job records."""
    cutoff = datetime.now().timestamp() - PPT_JOB_TTL_SECONDS
    expired_paths: List[pathlib.Path] = []
    with PPT_JOBS_LOCK:
        expired_ids = [
            job_id
            for job_id, job in PPT_JOBS.items()
            if job.status in {"completed", "failed", "downloading"} and job.updated_at <= cutoff
        ]
        for job_id in expired_ids:
            job = PPT_JOBS.pop(job_id)
            if job.file_path:
                expired_paths.append(job.file_path)

    for path in expired_paths:
        path.unlink(missing_ok=True)

    if not PPT_JOB_DIR.exists():
        return
    for path in PPT_JOB_DIR.iterdir():
        if path.is_file() and path.suffix == ".tmp" and path.stat().st_mtime <= cutoff:
            path.unlink(missing_ok=True)


def clear_ppt_job_storage() -> None:
    """PPT jobs are intentionally ephemeral; remove remnants after a restart."""
    shutil.rmtree(PPT_JOB_DIR, ignore_errors=True)
    PPT_JOB_DIR.mkdir(parents=True, exist_ok=True)
    with PPT_JOBS_LOCK:
        PPT_JOBS.clear()


def start_ppt_job_cleanup() -> None:
    global PPT_JOB_CLEANUP_THREAD
    if PPT_JOB_CLEANUP_THREAD and PPT_JOB_CLEANUP_THREAD.is_alive():
        return
    PPT_JOB_CLEANUP_STOP.clear()

    def run() -> None:
        while not PPT_JOB_CLEANUP_STOP.wait(PPT_JOB_CLEANUP_INTERVAL_SECONDS):
            cleanup_ppt_jobs()

    PPT_JOB_CLEANUP_THREAD = threading.Thread(target=run, name="ppt-job-cleanup", daemon=True)
    PPT_JOB_CLEANUP_THREAD.start()


def remove_ppt_job(job_id: str) -> None:
    with PPT_JOBS_LOCK:
        job = PPT_JOBS.pop(job_id, None)
    if job and job.file_path:
        job.file_path.unlink(missing_ok=True)


def version_options_for_rows(entries: List[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
    options: Dict[str, Dict[str, Dict[str, Any]]] = {}
    for entry in entries:
        sample = entry["sample"]
        key = entry["version_key"]
        options.setdefault(sample, {})[key] = {
            "key": key,
            "label": entry["version_label"],
            "num": entry["version_num"],
        }
    return {
        sample: sorted(sample_options.values(), key=lambda item: (item["num"], item["label"]))
        for sample, sample_options in options.items()
    }


def collect_report_data(
    req: ReportPptRequest,
    request: Request,
    db: Session,
    *,
    include_ipf_images: bool = False,
    user: Optional[Dict[str, str]] = None,
) -> Dict[str, Any]:
    selected_sample = req.sample.strip()
    golden_sample = (req.golden or "").strip()
    min_grain_size = validate_min_grain_size(req.min_grain_size)
    ipf_reference_vector = normalize_reference_vector(req.ipf_reference_vector)
    ipf_denoindex_threshold = validate_ipf_denoindex_threshold(req.ipf_denoindex_threshold)
    ipf_denoindex_misorientation_threshold = validate_ipf_denoindex_misorientation_threshold(
        req.ipf_denoindex_misorientation_threshold
    )
    if not selected_sample:
        raise HTTPException(status_code=400, detail="sample is required")
    if golden_sample and selected_sample == golden_sample:
        raise HTTPException(status_code=400, detail="sample and golden must be different")

    user = user or current_user(request)
    rows = db.execute(
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user["id"])
        .order_by(UserEbsdPair.created_at.desc())
    ).scalars().all()

    needed = {selected_sample}
    if golden_sample:
        needed.add(golden_sample)
    entries: List[Dict[str, Any]] = []
    for row in rows:
        sample, version_key, version_label, version_num = resolve_retest_version(
            row.sample,
            row.version_key,
            row.version_label,
            row.version_num,
        )
        if sample not in needed:
            continue
        entries.append({
            "row": row,
            "sample": sample,
            "position": row.position,
            "version_key": version_key,
            "version_label": version_label,
            "version_num": version_num,
        })

    if not any(entry["sample"] == selected_sample for entry in entries):
        raise HTTPException(status_code=404, detail="analysis sample not found in file library")
    if golden_sample and not any(entry["sample"] == golden_sample for entry in entries):
        raise HTTPException(status_code=404, detail="golden sample not found in file library")

    version_options = version_options_for_rows(entries)
    selected_options = version_options.get(selected_sample, [])
    golden_options = version_options.get(golden_sample, [])
    selected_key = req.version_key or (selected_options[-1]["key"] if selected_options else "")
    golden_key = golden_options[-1]["key"] if golden_options else ""
    selected_option = next((opt for opt in selected_options if opt["key"] == selected_key), None)
    golden_option = next((opt for opt in golden_options if opt["key"] == golden_key), None)
    if not selected_option:
        raise HTTPException(status_code=400, detail="invalid sample version_key")
    if golden_sample and not golden_option:
        raise HTTPException(status_code=400, detail="invalid golden sample")

    raw_data: Dict[str, Dict[str, Dict[str, Dict[str, Any]]]] = {}
    raw_pairs: Dict[str, Dict[str, Dict[str, UserEbsdPair]]] = {}
    raw_positions: Dict[str, Dict[str, set]] = {}
    for entry in entries:
        features = cpp_features(entry["row"], min_grain_size)
        sample = entry["sample"]
        version_key = entry["version_key"]
        position = entry["position"]
        raw_data.setdefault(sample, {}).setdefault(version_key, {})[position] = features
        raw_pairs.setdefault(sample, {}).setdefault(version_key, {})[position] = entry["row"]
        raw_positions.setdefault(sample, {}).setdefault(version_key, set()).add(position)

    cumulative: Dict[str, Dict[str, Dict[str, Dict[str, Any]]]] = {}
    cumulative_pairs: Dict[str, Dict[str, Dict[str, UserEbsdPair]]] = {}
    for sample, options in version_options.items():
        acc: Dict[str, Dict[str, Any]] = {}
        pair_acc: Dict[str, UserEbsdPair] = {}
        cumulative[sample] = {}
        cumulative_pairs[sample] = {}
        for opt in options:
            patch = raw_data.get(sample, {}).get(opt["key"], {})
            pair_patch = raw_pairs.get(sample, {}).get(opt["key"], {})
            acc = {**acc, **patch}
            pair_acc = {**pair_acc, **pair_patch}
            cumulative[sample][opt["key"]] = {**acc}
            cumulative_pairs[sample][opt["key"]] = {**pair_acc}

    merged_latest: Dict[str, Dict[str, Dict[str, Any]]] = {}
    for sample, options in version_options.items():
        latest_key = options[-1]["key"] if options else ""
        if latest_key:
            merged_latest[sample] = cumulative.get(sample, {}).get(latest_key, {})

    raw_version_positions = {
        sample: {
            version_key: sorted(pos_set)
            for version_key, pos_set in versions.items()
        }
        for sample, versions in raw_positions.items()
    }
    previous_positions: set[str] = set()
    selected_overridden_positions: List[str] = []
    for option in selected_options:
        version_positions = raw_positions.get(selected_sample, {}).get(option["key"], set())
        if option["key"] == selected_key:
            selected_overridden_positions = sorted(version_positions & previous_positions)
            break
        previous_positions.update(version_positions)

    ipf_images: Dict[str, bytes] = {}
    if include_ipf_images:
        for pos, row in cumulative_pairs.get(selected_sample, {}).get(selected_key, {}).items():
            try:
                ipf_images[pos] = cpp_ipf_map(
                    row,
                    ipf_reference_vector,
                    ipf_denoindex_threshold,
                    ipf_denoindex_misorientation_threshold,
                )
            except HTTPException:
                continue

    return {
        "selected_sample": selected_sample,
        "golden_sample": golden_sample,
        "selected_version_key": selected_key,
        "golden_version_key": golden_key,
        "selected_version_label": selected_option["label"],
        "golden_version_label": golden_option["label"] if golden_option else "",
        "report_data": merged_latest,
        "versioned_report_data": cumulative,
        "raw_version_positions": raw_version_positions,
        "selected_overridden_positions": selected_overridden_positions,
        "cumulative_pairs": cumulative_pairs,
        "ipf_images": ipf_images,
    }


def generate_report_data(req: ReportPptRequest, request: Request, db: Session = Depends(get_db)):
    context = collect_report_data(req, request, db)
    return {
        "reportData": context["report_data"],
        "versionedReportData": context["versioned_report_data"],
        "rawVersionPositions": context["raw_version_positions"],
    }


def build_ppt_report_context(
    req: ReportPptRequest,
    request: Request,
    db: Session,
    *,
    user: Optional[Dict[str, str]] = None,
) -> Dict[str, Any]:
    context = collect_report_data(req, request, db, user=user)
    selected_sample = context["selected_sample"]
    golden_sample = context["golden_sample"]
    selected_key = context["selected_version_key"]
    golden_key = context["golden_version_key"]
    selected_pairs = context["cumulative_pairs"].get(selected_sample, {}).get(selected_key, {})
    golden_pairs = context["cumulative_pairs"].get(golden_sample, {}).get(golden_key, {}) if golden_sample else {}
    selected_snapshot = context["versioned_report_data"].get(selected_sample, {}).get(selected_key, {})
    golden_snapshot = context["versioned_report_data"].get(golden_sample, {}).get(golden_key, {}) if golden_sample else {}

    ipf_uses_golden = req.ipf_display_target == "golden" and bool(golden_sample)
    ipf_display_sample = golden_sample if ipf_uses_golden else selected_sample
    ipf_display_pairs = golden_pairs if ipf_uses_golden else selected_pairs
    ipf_pair_for_legend = next(iter(ipf_display_pairs.values()), None)
    grid_uses_golden = req.grid_display_target == "golden" and bool(golden_sample)
    grid_display_sample = golden_sample if grid_uses_golden else selected_sample
    grid_display_version_label = context["golden_version_label"] if grid_uses_golden else context["selected_version_label"]

    requested_min_size = validate_min_grain_size(req.min_grain_size) or 10
    statistics_by_min_grain_size = {requested_min_size: selected_snapshot}

    vectors = report_ipf_vectors(req.ipf_reference_vector)

    ipf_images_by_direction: Dict[str, Dict[str, bytes]] = {}
    for direction, vector in vectors.items():
        images: Dict[str, bytes] = {}
        for position, row in ipf_display_pairs.items():
            try:
                images[position] = cpp_ipf_map(
                    row,
                    vector,
                    validate_ipf_denoindex_threshold(req.ipf_denoindex_threshold),
                    validate_ipf_denoindex_misorientation_threshold(req.ipf_denoindex_misorientation_threshold),
                )
            except HTTPException:
                continue
        ipf_images_by_direction[direction] = images

    selected_ipf_images_by_direction: Dict[str, Dict[str, bytes]]
    if not ipf_uses_golden:
        selected_ipf_images_by_direction = ipf_images_by_direction
    else:
        selected_ipf_images_by_direction = {}
        for direction, vector in vectors.items():
            images: Dict[str, bytes] = {}
            for position, row in selected_pairs.items():
                try:
                    images[position] = cpp_ipf_map(
                        row,
                        vector,
                        validate_ipf_denoindex_threshold(req.ipf_denoindex_threshold),
                        validate_ipf_denoindex_misorientation_threshold(req.ipf_denoindex_misorientation_threshold),
                    )
                except HTTPException:
                    continue
            selected_ipf_images_by_direction[direction] = images

    scan_report_images: Dict[str, Dict[str, Any]] = {}
    for position, row in selected_pairs.items():
        features = selected_snapshot.get(position, {})
        try:
            python_figures = generate_python_scan_figures(
                crc_content=row.pair.crc_file.content,
                cpr_content=row.pair.cpr_file.content,
                min_grain_size=requested_min_size,
                orientation_family=str(features.get("orientation_family", "cubic")),
            )
            python_images = {
                name: cpp_scale_bar(image) if isinstance(image, RawRgbMap) else image
                for name, image in python_figures.items()
            }
            scan_report_images[position] = {
                **python_images,
                "band_contrast": cpp_band_contrast_map(row),
                "pole_figure": cpp_pole_figure(row),
                "inverse_pole_figure": cpp_inverse_pole_figure(row),
                "ipf_maps": {
                    direction: images[position]
                    for direction, images in selected_ipf_images_by_direction.items()
                    if position in images
                },
            }
        except HTTPException:
            raise
        except Exception as exc:
            raise HTTPException(
                status_code=502,
                detail=f"Scan figure generation failed for {position}: {exc}",
            ) from exc

    report_data = {
        sample: snapshot
        for sample, snapshot in ((selected_sample, selected_snapshot), (golden_sample, golden_snapshot))
        if sample
    }
    analysis_data: Dict[str, Dict[str, float]] = {}
    if golden_sample and req.selected_features:
        analysis_data = compute_analysis(req.selected_features, golden_sample, report_data)

    if req.hist_bin_start_mode not in {"min", "zero"}:
        raise HTTPException(status_code=400, detail="hist_bin_start_mode must be min or zero")
    if req.hist_bin_width_mode not in {"ratio", "absolute"}:
        raise HTTPException(status_code=400, detail="hist_bin_width_mode must be ratio or absolute")
    if req.grain_chart_mode not in {"cdf", "hist", "areaHist"}:
        raise HTTPException(status_code=400, detail="grain_chart_mode must be cdf, hist, or areaHist")
    if req.histogram_y_mode not in {"count", "ratio"}:
        raise HTTPException(status_code=400, detail="histogram_y_mode must be count or ratio")
    if req.orientation_line_y_max is not None and (
        not math.isfinite(req.orientation_line_y_max) or req.orientation_line_y_max <= 0
    ):
        raise HTTPException(status_code=400, detail="orientation_line_y_max must be a positive number")
    if req.ipf_display_target not in {"analysis", "golden"}:
        raise HTTPException(status_code=400, detail="ipf_display_target must be analysis or golden")
    if req.grid_display_target not in {"analysis", "golden"}:
        raise HTTPException(status_code=400, detail="grid_display_target must be analysis or golden")
    if len(req.color_thresholds) != 2 or not 0 <= req.color_thresholds[0] <= req.color_thresholds[1] <= 100:
        raise HTTPException(status_code=400, detail="color_thresholds must contain two ascending values between 0 and 100")
    return {
        "selected_sample": selected_sample,
        "golden_sample": golden_sample,
        "selected_version_label": context["selected_version_label"],
        "golden_version_label": context["golden_version_label"],
        "ipf_display_sample": ipf_display_sample,
        "grid_display_sample": grid_display_sample,
        "grid_display_version_label": grid_display_version_label,
        "report_data": report_data,
        "ipf_images_by_direction": ipf_images_by_direction,
        "ipf_legend": cpp_pair_ipf_legend(ipf_pair_for_legend) if ipf_pair_for_legend else cpp_ipf_legend(),
        "scan_report_images": scan_report_images,
        "statistics_by_min_grain_size": statistics_by_min_grain_size,
        "analysis_data": analysis_data,
        "analysis_features": req.selected_features,
        "color_thresholds": req.color_thresholds,
        "overridden_positions": context["selected_overridden_positions"],
        "histogram_settings": {
            "start_mode": req.hist_bin_start_mode,
            "width_mode": req.hist_bin_width_mode,
            "width_ratio": req.hist_bin_width_ratio,
            "width_absolute": req.hist_bin_width_absolute,
            "chart_mode": req.grain_chart_mode,
            "y_mode": req.histogram_y_mode,
        },
        "orientation_line_y_max": req.orientation_line_y_max,
        "show_microstructure_page": req.show_microstructure_page,
        "show_triangle_labels": req.show_triangle_labels,
        "show_orientation_line_labels": req.show_orientation_line_labels,
    }


def run_ppt_report_job(job_id: str, req: ReportPptRequest, user: Dict[str, str]) -> None:
    """Build a report outside the request lifecycle using a fresh DB session."""
    with PPT_JOBS_LOCK:
        job = PPT_JOBS.get(job_id)
        if not job:
            return
        job.status = "running"
        job.updated_at = datetime.now().timestamp()

    db = SessionLocal()
    try:
        context = build_ppt_report_context(req, None, db, user=user)
        pptx = build_pptx_report(**context)
        stamp = datetime.now().strftime("%Y-%m-%d")
        filename = f"EBSD-report-{context['selected_sample']}-{stamp}.pptx"
        PPT_JOB_DIR.mkdir(parents=True, exist_ok=True)
        path = PPT_JOB_DIR / f"{job_id}.pptx"
        temporary_path = PPT_JOB_DIR / f".{job_id}.pptx.tmp"
        with temporary_path.open("wb") as output:
            output.write(pptx)
        temporary_path.replace(path)

        with PPT_JOBS_LOCK:
            job = PPT_JOBS.get(job_id)
            if not job:
                path.unlink(missing_ok=True)
                return
            job.status = "completed"
            job.updated_at = datetime.now().timestamp()
            job.filename = filename
            job.file_path = path
    except HTTPException as exc:
        detail = exc.detail if isinstance(exc.detail, str) else "PPT output failed"
        with PPT_JOBS_LOCK:
            job = PPT_JOBS.get(job_id)
            if job:
                job.status = "failed"
                job.updated_at = datetime.now().timestamp()
                job.error = detail
    except Exception as exc:
        with PPT_JOBS_LOCK:
            job = PPT_JOBS.get(job_id)
            if job:
                job.status = "failed"
                job.updated_at = datetime.now().timestamp()
                job.error = f"PPT output failed: {exc}"
    finally:
        db.close()


def get_owned_ppt_job(job_id: str, user_id: str) -> PptJob:
    with PPT_JOBS_LOCK:
        job = PPT_JOBS.get(job_id)
        if not job or job.user_id != user_id:
            raise HTTPException(status_code=404, detail="PPT job not found")
        return job


def create_ppt_report_job(req: ReportPptRequest, request: Request):
    user = current_user(request)
    cleanup_ppt_jobs()
    now = datetime.now().timestamp()
    with PPT_JOBS_LOCK:
        if any(job.status in {"queued", "running"} for job in PPT_JOBS.values()):
            raise HTTPException(status_code=409, detail="a PPT report is already being generated")
        job = PptJob(id=uuid.uuid4().hex, user_id=user["id"], created_at=now, updated_at=now)
        PPT_JOBS[job.id] = job
    PPT_JOB_EXECUTOR.submit(run_ppt_report_job, job.id, req, user)
    return ppt_job_payload(job)


def get_ppt_report_job(job_id: str, request: Request):
    user = current_user(request)
    cleanup_ppt_jobs()
    return ppt_job_payload(get_owned_ppt_job(job_id, user["id"]))


def download_ppt_report_job(job_id: str, request: Request):
    user = current_user(request)
    cleanup_ppt_jobs()
    job = get_owned_ppt_job(job_id, user["id"])
    with PPT_JOBS_LOCK:
        if job.status != "completed" or not job.file_path or not job.file_path.is_file():
            raise HTTPException(status_code=409, detail="PPT report is not ready for download")
        job.status = "downloading"
        job.updated_at = datetime.now().timestamp()
        path = job.file_path
        filename = job.filename or "EBSD-report.pptx"
    return FileResponse(
        path,
        media_type=PPTX_MEDIA_TYPE,
        filename=filename,
        background=BackgroundTask(remove_ppt_job, job_id),
    )

