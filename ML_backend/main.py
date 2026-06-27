import inspect
import json
import os
import pathlib
import re
import shutil
import urllib.error
import urllib.request
import uuid
from datetime import datetime
from urllib.parse import quote, urlencode
from typing import Annotated, Any, Dict, List, Optional, Tuple
from fastapi import Depends, FastAPI, File, Form, HTTPException, Request, Response, UploadFile
import defdap.ebsd as ebsd # type: ignore
import tempfile
import numpy as np
import joblib
from pydantic import BaseModel, TypeAdapter, ValidationError # type: ignore
from ebsd_utils import *
from typing import Callable, Any
from sqlalchemy.orm import Session, load_only, selectinload
from ebsd_store import (
    EbsdFile,
    EbsdPair,
    UserEbsdTarget,
    UserEbsdPair,
    current_user_from_go_session,
    get_db,
    get_or_create_ebsd_file,
    get_or_create_pair,
    init_db,
    read_upload,
    upsert_user_target,
    upsert_user_pair,
)
from ppt_report import build_pptx_report
from sqlalchemy import func, select

app = FastAPI()

CPP_BACKEND_URL = os.getenv("CPP_BACKEND_URL", "http://cpp_backend:8080").rstrip("/")
CPP_BACKEND_TIMEOUT_SECONDS = int(os.getenv("CPP_BACKEND_TIMEOUT_SECONDS", "180"))


def attachment_content_disposition(filename: str) -> str:
    fallback = re.sub(r"[^A-Za-z0-9._-]+", "_", filename or "ebsd-file").strip("._")
    fallback = fallback or "ebsd-file"
    return f"attachment; filename=\"{fallback}\"; filename*=UTF-8''{quote(filename or fallback, safe='')}"


def resolve_retest_version(sample: str, version_key: Optional[str] = None, version_label: Optional[str] = None, version_num: Optional[int] = None):
    clean_sample = sample.strip()
    match = re.match(r"^(.*?)-(\d+)$", clean_sample)
    base_sample = clean_sample
    suffix_num: Optional[int] = None
    if match:
        base_sample = match.group(1).strip() or clean_sample
        suffix_num = int(match.group(2))

    if version_key or version_label or version_num:
        parsed_num = version_num or suffix_num or 1
        return base_sample, f"v{parsed_num}", f"{base_sample}-{parsed_num}", parsed_num

    if not match:
        return clean_sample, "v1", f"{clean_sample}-1", 1

    parsed_num = suffix_num or 1
    return base_sample, f"v{parsed_num}", f"{base_sample}-{parsed_num}", parsed_num


def cleanup_orphan_pair_and_files(db: Session, pair: EbsdPair) -> Dict[str, int]:
    pair_ref_count = db.execute(
        select(func.count())
        .select_from(UserEbsdPair)
        .where(UserEbsdPair.pair_id == pair.id)
    ).scalar_one()
    if pair_ref_count > 0:
        return {"pairs": 0, "files": 0}

    file_ids = [pair.crc_file_id, pair.cpr_file_id]
    db.delete(pair)
    db.flush()

    deleted_files = 0
    for file_id in file_ids:
        file_ref_count = db.execute(
            select(func.count())
            .select_from(EbsdPair)
            .where((EbsdPair.crc_file_id == file_id) | (EbsdPair.cpr_file_id == file_id))
        ).scalar_one()
        if file_ref_count == 0:
            stored_file = db.get(EbsdFile, file_id)
            if stored_file:
                db.delete(stored_file)
                deleted_files += 1

    db.flush()
    return {"pairs": 1, "files": deleted_files}

@app.on_event("startup")
def startup():
    init_db()

@app.get('/')
def hello():
    return 'Hello world!'

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
    "[110] (deviation 20%)": numeric_compare,
    "[111] (deviation 20%)": numeric_compare,
    "[001] (deviation 15%)": numeric_compare,
    "[110] (deviation 15%)": numeric_compare,
    "[111] (deviation 15%)": numeric_compare
}

@app.get('/ebsd_features')
def ebsd_features():
    return [*GRAIN_FEATURES.keys(), *ORIENTATION_FEATURES.keys()]


@app.get('/ebsd/targets')
def list_ebsd_targets(request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    target_rows = db.execute(
        select(UserEbsdTarget)
        .where(UserEbsdTarget.user_id == user["id"])
        .order_by(UserEbsdTarget.name.asc())
    ).scalars().all()
    pair_rows = db.execute(
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user["id"])
    ).scalars().all()

    merged = {}
    for row in target_rows:
        name, _, _, _ = resolve_retest_version(row.name)
        if name not in merged:
            merged[name] = {
                "id": row.id,
                "name": name,
                "created_at": row.created_at,
                "updated_at": row.updated_at,
            }
    for row in pair_rows:
        name, _, _, _ = resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)
        if name not in merged:
            merged[name] = {
                "id": row.id,
                "name": name,
                "created_at": row.created_at,
                "updated_at": row.updated_at,
            }

    return sorted(merged.values(), key=lambda item: item["name"].lower())


def _user_pair_metadata_query(user_id: str):
    return (
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user_id)
        .options(
            load_only(
                UserEbsdPair.id,
                UserEbsdPair.user_id,
                UserEbsdPair.pair_id,
                UserEbsdPair.display_name,
                UserEbsdPair.sample,
                UserEbsdPair.position,
                UserEbsdPair.folder,
                UserEbsdPair.version_key,
                UserEbsdPair.version_label,
                UserEbsdPair.version_num,
                UserEbsdPair.created_at,
                UserEbsdPair.updated_at,
            ),
            selectinload(UserEbsdPair.pair).options(
                load_only(EbsdPair.id, EbsdPair.pair_hash, EbsdPair.crc_file_id, EbsdPair.cpr_file_id, EbsdPair.created_at),
                selectinload(EbsdPair.crc_file).load_only(
                    EbsdFile.id,
                    EbsdFile.original_filename,
                    EbsdFile.size_bytes,
                    EbsdFile.content_hash,
                    EbsdFile.extension,
                    EbsdFile.created_at,
                ),
                selectinload(EbsdPair.cpr_file).load_only(
                    EbsdFile.id,
                    EbsdFile.original_filename,
                    EbsdFile.size_bytes,
                    EbsdFile.content_hash,
                    EbsdFile.extension,
                    EbsdFile.created_at,
                ),
            ),
        )
    )


def serialize_user_pair_metadata(row: UserEbsdPair) -> Dict[str, Any]:
    sample, version_key, version_label, version_num = resolve_retest_version(
        row.sample,
        row.version_key,
        row.version_label,
        row.version_num,
    )
    return {
        "id": row.id,
        "pair_id": row.pair_id,
        "pair_hash": row.pair.pair_hash,
        "crc_filename": row.pair.crc_file.original_filename,
        "crc_size_bytes": row.pair.crc_file.size_bytes,
        "crc_hash": row.pair.crc_file.content_hash,
        "cpr_filename": row.pair.cpr_file.original_filename,
        "cpr_size_bytes": row.pair.cpr_file.size_bytes,
        "cpr_hash": row.pair.cpr_file.content_hash,
        "sample": sample,
        "position": row.position,
        "name": row.display_name,
        "folder": row.folder,
        "version_key": version_key,
        "version_label": version_label,
        "version_num": version_num,
        "created_at": row.created_at,
        "updated_at": row.updated_at,
    }


@app.get('/ebsd/library/summary')
def get_ebsd_library_summary(request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    target_rows = db.execute(
        select(UserEbsdTarget)
        .where(UserEbsdTarget.user_id == user["id"])
        .order_by(UserEbsdTarget.name.asc())
    ).scalars().all()
    pair_rows = db.execute(
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user["id"])
        .options(
            load_only(
                UserEbsdPair.id,
                UserEbsdPair.sample,
                UserEbsdPair.position,
                UserEbsdPair.version_key,
                UserEbsdPair.version_label,
                UserEbsdPair.version_num,
                UserEbsdPair.created_at,
                UserEbsdPair.updated_at,
            )
        )
    ).scalars().all()

    targets: Dict[str, Dict[str, Any]] = {}
    for row in target_rows:
        name, _, _, _ = resolve_retest_version(row.name)
        targets[name] = {
            "id": row.id,
            "name": name,
            "pair_count": 0,
            "position_count": 0,
            "version_count": 0,
            "latest_version_label": "",
            "created_at": row.created_at,
            "updated_at": row.updated_at,
            "positions": set(),
            "versions": {},
        }

    for row in pair_rows:
        sample, version_key, version_label, version_num = resolve_retest_version(
            row.sample,
            row.version_key,
            row.version_label,
            row.version_num,
        )
        target = targets.setdefault(sample, {
            "id": row.id,
            "name": sample,
            "pair_count": 0,
            "position_count": 0,
            "version_count": 0,
            "latest_version_label": "",
            "created_at": row.created_at,
            "updated_at": row.updated_at,
            "positions": set(),
            "versions": {},
        })
        target["pair_count"] += 1
        target["positions"].add(row.position)
        target["versions"][version_key] = {"label": version_label, "num": version_num}
        if row.updated_at and row.updated_at > target["updated_at"]:
            target["updated_at"] = row.updated_at

    normalized_targets = []
    for target in targets.values():
        versions = sorted(target.pop("versions").values(), key=lambda item: (item["num"], item["label"]))
        positions = target.pop("positions")
        target["position_count"] = len(positions)
        target["version_count"] = len(versions)
        target["latest_version_label"] = versions[-1]["label"] if versions else ""
        normalized_targets.append(target)

    normalized_targets.sort(key=lambda item: item["name"].lower())
    return {
        "target_count": len(normalized_targets),
        "pair_count": len(pair_rows),
        "targets": normalized_targets,
    }


@app.post('/ebsd/targets')
async def create_ebsd_target(
    request: Request,
    name: str = Form(...),
    db: Session = Depends(get_db),
):
    user = current_user_from_go_session(request)
    target, created = upsert_user_target(db, user["id"], name)
    db.commit()
    return {
        "id": target.id,
        "name": target.name,
        "created": created,
        "created_at": target.created_at,
        "updated_at": target.updated_at,
    }


@app.post('/ebsd/pairs')
async def save_ebsd_pair(
    request: Request,
    crc: UploadFile = File(...),
    cpr: UploadFile = File(...),
    sample: str = Form(...),
    position: str = Form(...),
    name: str = Form(""),
    folder: str = Form(""),
    version_key: Optional[str] = Form(None),
    version_label: Optional[str] = Form(None),
    version_num: Optional[int] = Form(None),
    db: Session = Depends(get_db),
):
    user = current_user_from_go_session(request)
    crc_content, crc_filename = await read_upload(crc, ".crc")
    cpr_content, cpr_filename = await read_upload(cpr, ".cpr")

    if not sample.strip() or not position.strip():
        raise HTTPException(status_code=400, detail="sample and position are required")

    clean_sample, resolved_version_key, resolved_version_label, resolved_version_num = resolve_retest_version(
        sample,
        version_key.strip() if version_key else None,
        version_label.strip() if version_label else None,
        version_num,
    )

    upsert_user_target(db, user["id"], clean_sample)
    crc_file, crc_created = get_or_create_ebsd_file(db, crc_content, crc_filename, ".crc")
    cpr_file, cpr_created = get_or_create_ebsd_file(db, cpr_content, cpr_filename, ".cpr")
    pair, pair_created = get_or_create_pair(db, crc_file, cpr_file)
    user_pair, user_pair_created = upsert_user_pair(
        db,
        user["id"],
        pair,
        display_name=name.strip() or f"{clean_sample}-{position}",
        sample=clean_sample,
        position=position.strip(),
        folder=folder.strip() or None,
        version_key=resolved_version_key,
        version_label=resolved_version_label,
        version_num=resolved_version_num,
    )
    db.commit()

    return {
        "id": user_pair.id,
        "pair_id": pair.id,
        "pair_hash": pair.pair_hash,
        "crc_hash": crc_file.content_hash,
        "cpr_hash": cpr_file.content_hash,
        "created": {
            "crc_file": crc_created,
            "cpr_file": cpr_created,
            "pair": pair_created,
            "user_pair": user_pair_created,
        },
    }


@app.get('/ebsd/pairs')
def list_ebsd_pairs(request: Request, db: Session = Depends(get_db)):
    return list_ebsd_pair_metadata(request, db)


@app.get('/ebsd/pairs/metadata')
def list_ebsd_pair_metadata(request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    rows = db.execute(
        _user_pair_metadata_query(user["id"])
        .order_by(UserEbsdPair.created_at.desc())
    ).scalars().all()

    return [serialize_user_pair_metadata(row) for row in rows]


@app.get('/ebsd/pairs/{user_pair_id}/files/{kind}')
def get_ebsd_pair_file(
    user_pair_id: str,
    kind: str,
    request: Request,
    db: Session = Depends(get_db),
):
    user = current_user_from_go_session(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")

    if kind == "crc":
        stored_file = row.pair.crc_file
    elif kind == "cpr":
        stored_file = row.pair.cpr_file
    else:
        raise HTTPException(status_code=400, detail="kind must be crc or cpr")

    return Response(
        content=stored_file.content,
        media_type="application/octet-stream",
        headers={
            "Content-Disposition": attachment_content_disposition(stored_file.original_filename),
            "X-File-Hash": stored_file.content_hash,
        },
    )


@app.delete('/ebsd/pairs/{user_pair_id}')
def delete_ebsd_pair(user_pair_id: str, request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")

    pair = row.pair
    db.delete(row)
    db.flush()
    cleanup = cleanup_orphan_pair_and_files(db, pair)
    db.commit()
    return {"deleted_user_pairs": 1, **cleanup}


@app.delete('/ebsd/targets/{target_name}')
def delete_ebsd_target(target_name: str, request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    clean_target, _, _, _ = resolve_retest_version(target_name)

    pair_rows = db.execute(
        select(UserEbsdPair).where(UserEbsdPair.user_id == user["id"])
    ).scalars().all()
    target_rows = db.execute(
        select(UserEbsdTarget).where(UserEbsdTarget.user_id == user["id"])
    ).scalars().all()

    deleted_user_pairs = 0
    deleted_pairs = 0
    deleted_files = 0
    for row in pair_rows:
        row_sample, _, _, _ = resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)
        if row_sample != clean_target:
            continue

        pair = row.pair
        db.delete(row)
        db.flush()
        cleanup = cleanup_orphan_pair_and_files(db, pair)
        deleted_user_pairs += 1
        deleted_pairs += cleanup["pairs"]
        deleted_files += cleanup["files"]

    deleted_targets = 0
    for row in target_rows:
        row_target, _, _, _ = resolve_retest_version(row.name)
        if row_target == clean_target:
            db.delete(row)
            deleted_targets += 1

    db.commit()
    return {
        "deleted_targets": deleted_targets,
        "deleted_user_pairs": deleted_user_pairs,
        "pairs": deleted_pairs,
        "files": deleted_files,
    }


class ReportPptRequest(BaseModel):
    sample: str
    golden: str
    version_key: Optional[str] = None
    min_grain_size: Optional[int] = None


def validate_min_grain_size(value: Optional[int]) -> Optional[int]:
    if value is None:
        return None
    if value < 1 or value > 1000000:
        raise HTTPException(status_code=400, detail="min_grain_size must be between 1 and 1000000")
    return value


def cpp_backend_candidates() -> List[str]:
    urls = [CPP_BACKEND_URL]
    if CPP_BACKEND_URL != "http://127.0.0.1:8080":
        urls.append("http://127.0.0.1:8080")
    return urls


def multipart_body(parts: List[Tuple[str, str, str, bytes]]) -> Tuple[bytes, str]:
    boundary = f"----ebsd-report-{uuid.uuid4().hex}"
    body = bytearray()
    for name, filename, content_type, content in parts:
        safe_filename = filename.replace('"', "_")
        body.extend(f"--{boundary}\r\n".encode("utf-8"))
        body.extend(
            f'Content-Disposition: form-data; name="{name}"; filename="{safe_filename}"\r\n'.encode("utf-8")
        )
        body.extend(f"Content-Type: {content_type}\r\n\r\n".encode("utf-8"))
        body.extend(content)
        body.extend(b"\r\n")
    body.extend(f"--{boundary}--\r\n".encode("utf-8"))
    return bytes(body), boundary


def post_cpp_pair(endpoint: str, row: UserEbsdPair, accept: str, min_grain_size: Optional[int] = None) -> bytes:
    body, boundary = multipart_body([
        ("crc", row.pair.crc_file.original_filename, "application/octet-stream", row.pair.crc_file.content),
        ("cpr", row.pair.cpr_file.original_filename, "application/octet-stream", row.pair.cpr_file.content),
    ])
    last_error: Optional[Exception] = None
    for base_url in cpp_backend_candidates():
        url = f"{base_url}{endpoint}"
        if min_grain_size is not None:
            url = f"{url}?{urlencode({'min_grain_size': min_grain_size})}"
        req = urllib.request.Request(
            url,
            data=body,
            headers={
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Accept": accept,
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=CPP_BACKEND_TIMEOUT_SECONDS) as res:
                return res.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise HTTPException(status_code=502, detail=f"C++ backend {endpoint} failed: {detail}")
        except Exception as exc:
            last_error = exc
    raise HTTPException(status_code=502, detail=f"C++ backend unavailable: {last_error}")


def cpp_features(row: UserEbsdPair, min_grain_size: Optional[int] = None) -> Dict[str, Any]:
    raw = post_cpp_pair("/features", row, "application/json", min_grain_size)
    try:
        data = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=502, detail=f"C++ backend returned invalid feature JSON: {exc}")
    if not isinstance(data, dict):
        raise HTTPException(status_code=502, detail="C++ backend returned invalid feature payload")
    return data


def cpp_ipf_map(row: UserEbsdPair) -> bytes:
    return post_cpp_pair("/ipf_map", row, "image/png")


@app.get('/ebsd/pairs/{user_pair_id}/ipf_map')
def get_ebsd_pair_ipf_map(user_pair_id: str, request: Request, db: Session = Depends(get_db)):
    user = current_user_from_go_session(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")

    return Response(content=cpp_ipf_map(row), media_type="image/png")


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
) -> Dict[str, Any]:
    selected_sample = req.sample.strip()
    golden_sample = req.golden.strip()
    min_grain_size = validate_min_grain_size(req.min_grain_size)
    if not selected_sample or not golden_sample:
        raise HTTPException(status_code=400, detail="sample and golden are required")
    if selected_sample == golden_sample:
        raise HTTPException(status_code=400, detail="sample and golden must be different")

    user = current_user_from_go_session(request)
    rows = db.execute(
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user["id"])
        .order_by(UserEbsdPair.created_at.desc())
    ).scalars().all()

    needed = {selected_sample, golden_sample}
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
    if not any(entry["sample"] == golden_sample for entry in entries):
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
    if not golden_option:
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

    ipf_images: Dict[str, bytes] = {}
    if include_ipf_images:
        for pos, row in cumulative_pairs.get(selected_sample, {}).get(selected_key, {}).items():
            try:
                ipf_images[pos] = cpp_ipf_map(row)
            except HTTPException:
                continue

    return {
        "selected_sample": selected_sample,
        "golden_sample": golden_sample,
        "selected_version_key": selected_key,
        "golden_version_key": golden_key,
        "selected_version_label": selected_option["label"],
        "golden_version_label": golden_option["label"],
        "report_data": merged_latest,
        "versioned_report_data": cumulative,
        "raw_version_positions": raw_version_positions,
        "cumulative_pairs": cumulative_pairs,
        "ipf_images": ipf_images,
    }


@app.post('/reports/data')
def generate_report_data(req: ReportPptRequest, request: Request, db: Session = Depends(get_db)):
    context = collect_report_data(req, request, db)
    return {
        "reportData": context["report_data"],
        "versionedReportData": context["versioned_report_data"],
        "rawVersionPositions": context["raw_version_positions"],
    }


def build_ppt_report_context(req: ReportPptRequest, request: Request, db: Session) -> Dict[str, Any]:
    context = collect_report_data(req, request, db, include_ipf_images=True)
    selected_sample = context["selected_sample"]
    golden_sample = context["golden_sample"]
    selected_key = context["selected_version_key"]
    golden_key = context["golden_version_key"]
    return {
        "selected_sample": selected_sample,
        "golden_sample": golden_sample,
        "selected_version_label": context["selected_version_label"],
        "golden_version_label": context["golden_version_label"],
        "report_data": {
            selected_sample: context["versioned_report_data"].get(selected_sample, {}).get(selected_key, {}),
            golden_sample: context["versioned_report_data"].get(golden_sample, {}).get(golden_key, {}),
        },
        "ipf_images": context["ipf_images"],
    }


@app.post('/reports/pptx')
def generate_ppt_report(req: ReportPptRequest, request: Request, db: Session = Depends(get_db)):
    context = build_ppt_report_context(req, request, db)
    pptx = build_pptx_report(**context)
    stamp = datetime.now().strftime("%Y-%m-%d")
    filename = f"EBSD-report-{context['selected_sample']}-{stamp}.pptx"
    return Response(
        content=pptx,
        media_type="application/vnd.openxmlformats-officedocument.presentationml.presentation",
        headers={"Content-Disposition": attachment_content_disposition(filename)},
    )

class AnalysisRequest(BaseModel):
    features: List[str]
    golden: str
    # payload (from cpp_backend features + frontend result):
    # sample -> pos -> {"grains": [...], "orientation_ratio(20%)": [..3..], "orientation_ratio(15%)": [..3..]}
    data: Dict[str, Dict[str, Dict[str, Any]]]


@app.post('/analysis')
def analysis(req: AnalysisRequest):
    for feat in req.features:
        if feat not in GRAIN_FEATURES and feat not in ORIENTATION_FEATURES:
            raise HTTPException(status_code=400, detail="Invalid feature options")
    if req.golden not in req.data:
        raise HTTPException(status_code=400, detail="Invalid golden sample")
    res: Any = {}
    for (sample, posdata) in req.data.items():
        if sample == req.golden:
            continue
        for (pos, cur_data) in posdata.items():
            golden_data = req.data[req.golden].get(pos)
            if not golden_data:
                continue

            statRes: List[float] = []
            for feat in req.features:
                if feat in GRAIN_FEATURES:
                    if "grains" not in golden_data or "grains" not in cur_data:
                        continue
                    handler = GRAIN_FEATURES[feat]
                    statRes.append(handler(golden_data["grains"], cur_data["grains"]))
                elif feat in ORIENTATION_FEATURES:
                    handler = ORIENTATION_FEATURES[feat]
                    # feat example: "[001] (deviation 20%)"
                    try:
                        axis = feat.split(']')[0].lstrip('[')  # 001 / 110 / 111
                        deviation = "20" if "20%" in feat else "15"
                        ratio_key = f"orientation_ratio({deviation}%)"
                        idx_map = {"001": 0, "110": 1, "111": 2}
                        idx = idx_map[axis]
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
            

    

        
