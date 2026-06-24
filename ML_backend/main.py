import inspect
import os
import pathlib
import re
import shutil
from typing import Annotated, Any, Dict, List, Optional
from fastapi import Depends, FastAPI, File, Form, HTTPException, Request, Response, UploadFile
import defdap.ebsd as ebsd # type: ignore
import tempfile
import numpy as np
import joblib
from pydantic import BaseModel, TypeAdapter, ValidationError # type: ignore
from ebsd_utils import *
from typing import Callable, Any
from sqlalchemy.orm import Session
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
from sqlalchemy import func, select

app = FastAPI()


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
    user = current_user_from_go_session(request)
    rows = db.execute(
        select(UserEbsdPair)
        .where(UserEbsdPair.user_id == user["id"])
        .order_by(UserEbsdPair.created_at.desc())
    ).scalars().all()

    return [
        {
            "id": row.id,
            "pair_id": row.pair_id,
            "pair_hash": row.pair.pair_hash,
            "crc_filename": row.pair.crc_file.original_filename,
            "crc_size_bytes": row.pair.crc_file.size_bytes,
            "crc_hash": row.pair.crc_file.content_hash,
            "cpr_filename": row.pair.cpr_file.original_filename,
            "cpr_size_bytes": row.pair.cpr_file.size_bytes,
            "cpr_hash": row.pair.cpr_file.content_hash,
            "sample": resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)[0],
            "position": row.position,
            "name": row.display_name,
            "folder": row.folder,
            "version_key": resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)[1],
            "version_label": resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)[2],
            "version_num": resolve_retest_version(row.sample, row.version_key, row.version_label, row.version_num)[3],
            "created_at": row.created_at,
            "updated_at": row.updated_at,
        }
        for row in rows
    ]


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
            "Content-Disposition": f'attachment; filename="{stored_file.original_filename}"',
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
            

    

        
