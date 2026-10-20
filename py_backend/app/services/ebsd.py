import math
import os
import re
from urllib.parse import quote
from typing import Any, Dict, List, Optional
from fastapi import Depends, File, Form, HTTPException, Request, Response, UploadFile
from sqlalchemy.orm import Session, load_only, selectinload
from app.db.store import (
    EbsdFile,
    EbsdPair,
    UserEbsdTarget,
    UserEbsdPair,
    get_db,
    get_or_create_ebsd_file,
    get_or_create_pair,
    read_upload,
    upsert_user_target,
    create_user_pair,
)
from app.analysis.service import GRAIN_FEATURES, ORIENTATION_FEATURES
from app.integrations.cpp import (
    cpp_ipf_legend,
    cpp_ipf_map,
    cpp_material_metadata_from_files,
    cpp_pair_ipf_legend,
)
from app.services.auth import current_user
from sqlalchemy import func, select


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

    file_ids = {pair.crc_file_id, pair.cpr_file_id}
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

def lattice_distance(phase: Dict[str, Any], candidate: Dict[str, Any]) -> Optional[float]:
    if phase.get("space_group") and int(phase.get("space_group") or 0) != int(candidate["space_group"]):
        return None
    constants = phase.get("lattice_constants") or []
    if len(constants) < 6:
        return None
    a, b, c, alpha, beta, gamma = [float(v) for v in constants[:6]]
    if any(not math.isfinite(v) for v in (a, b, c, alpha, beta, gamma)):
        return None
    if candidate["phase"] in {"FCC", "BCC", "Diamond"}:
        if max(abs(alpha - 90), abs(beta - 90), abs(gamma - 90)) > 1.5:
            return None
        measured = (a + b + c) / 3
        cubic_spread = max(abs(a - measured), abs(b - measured), abs(c - measured)) / max(measured, 1e-9)
        if cubic_spread > 0.015:
            return None
        return abs(measured - float(candidate["a"])) / float(candidate["a"])
    if candidate["phase"] == "HCP":
        if abs(alpha - 90) > 1.5 or abs(beta - 90) > 1.5 or abs(gamma - 120) > 2.0:
            return None
        measured_a = (a + b) / 2
        hex_spread = abs(a - b) / max(measured_a, 1e-9)
        if hex_spread > 0.015:
            return None
        da = abs(measured_a - float(candidate["a"])) / float(candidate["a"])
        dc = abs(c - float(candidate["c"])) / float(candidate["c"])
        return math.sqrt((da * da + dc * dc) / 2)
    return None


def detect_material_from_metadata(metadata: Dict[str, Any]) -> Dict[str, Any]:
    phases = metadata.get("phases") if isinstance(metadata, dict) else []
    phase = phases[0] if phases else {}
    candidates = []
    for candidate in MATERIAL_PHASES:
        distance = lattice_distance(phase, candidate)
        if distance is None or distance > 0.025:
            continue
        score = distance
        candidates.append({**candidate, "distance": distance, "score": score})
    candidates.sort(key=lambda item: item["score"])
    best = candidates[0] if candidates else None
    matched_by: List[str] = []
    confidence = "unknown"
    ambiguous = False
    if best:
        matched_by.append("lattice")
        if best["distance"] <= 0.010:
            confidence = "high"
        elif best["distance"] <= 0.020:
            confidence = "medium"
        else:
            confidence = "low"
        if len(candidates) > 1 and abs(candidates[1]["score"] - best["score"]) < 0.006:
            ambiguous = True
            confidence = "ambiguous"

    element = best["element"] if best else None
    phase_label = best["phase"] if best else None
    material_key = best["key"] if best else "unknown"
    display_name = f"{element} ({phase_label})" if element and phase_label else (element or "Unknown")
    return {
        "material_key": material_key,
        "display_name": display_name,
        "element": element,
        "phase": phase_label,
        "confidence": confidence,
        "matched_by": matched_by or ["unknown"],
        "ambiguous": ambiguous,
        "raw": metadata,
        "candidates": [
            {"material_key": item["key"], "distance": item["distance"], "score": item["score"]}
            for item in candidates[:5]
        ],
    }

def ebsd_features():
    return [*GRAIN_FEATURES.keys(), *ORIENTATION_FEATURES.keys()]


def list_ebsd_targets(request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
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
                UserEbsdPair.target_id,
                UserEbsdPair.pair_id,
                UserEbsdPair.display_name,
                UserEbsdPair.sample,
                UserEbsdPair.position,
                UserEbsdPair.folder,
                UserEbsdPair.version_key,
                UserEbsdPair.version_label,
                UserEbsdPair.version_num,
                UserEbsdPair.material_key,
                UserEbsdPair.material_display_name,
                UserEbsdPair.material_element,
                UserEbsdPair.material_phase,
                UserEbsdPair.material_confidence,
                UserEbsdPair.material_source,
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
    primary_file = row.pair.cpr_file
    file_format = primary_file.extension.lstrip(".").lower()
    is_paired = file_format == "cpr"
    return {
        "id": row.id,
        "target_id": row.target_id,
        "pair_id": row.pair_id,
        "pair_hash": row.pair.pair_hash,
        "crc_filename": row.pair.crc_file.original_filename,
        "crc_size_bytes": row.pair.crc_file.size_bytes,
        "crc_hash": row.pair.crc_file.content_hash,
        "cpr_filename": row.pair.cpr_file.original_filename,
        "cpr_size_bytes": row.pair.cpr_file.size_bytes,
        "cpr_hash": row.pair.cpr_file.content_hash,
        "file_format": "cpr+crc" if is_paired else file_format,
        "is_paired_format": is_paired,
        "primary_filename": primary_file.original_filename,
        "primary_size_bytes": primary_file.size_bytes,
        "primary_hash": primary_file.content_hash,
        "sample": sample,
        "position": row.position,
        "name": row.display_name,
        "folder": row.folder,
        "version_key": version_key,
        "version_label": version_label,
        "version_num": version_num,
        "material_key": row.material_key,
        "material_display_name": row.material_display_name,
        "material_element": row.material_element,
        "material_phase": row.material_phase,
        "material_confidence": row.material_confidence,
        "material_source": row.material_source,
        "created_at": row.created_at,
        "updated_at": row.updated_at,
    }


def get_ebsd_library_summary(request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
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
                UserEbsdPair.material_key,
                UserEbsdPair.material_display_name,
                UserEbsdPair.material_confidence,
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
            "materials": {},
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
            "materials": {},
            "created_at": row.created_at,
            "updated_at": row.updated_at,
            "positions": set(),
            "versions": {},
        })
        target["pair_count"] += 1
        target["positions"].add(row.position)
        target["versions"][version_key] = {"label": version_label, "num": version_num}
        target["materials"][row.material_key or "unknown"] = {
            "key": row.material_key or "unknown",
            "display_name": row.material_display_name or "Unknown",
            "confidence": row.material_confidence or "unknown",
        }
        if row.updated_at and row.updated_at > target["updated_at"]:
            target["updated_at"] = row.updated_at

    normalized_targets = []
    for target in targets.values():
        versions = sorted(target.pop("versions").values(), key=lambda item: (item["num"], item["label"]))
        materials = sorted(target.pop("materials").values(), key=lambda item: item["display_name"])
        positions = target.pop("positions")
        target["position_count"] = len(positions)
        target["version_count"] = len(versions)
        target["latest_version_label"] = versions[-1]["label"] if versions else ""
        target["materials"] = materials
        normalized_targets.append(target)

    normalized_targets.sort(key=lambda item: item["name"].lower())
    return {
        "target_count": len(normalized_targets),
        "pair_count": len(pair_rows),
        "targets": normalized_targets,
    }


async def create_ebsd_target(
    request: Request,
    name: str = Form(...),
    db: Session = Depends(get_db),
):
    user = current_user(request)
    target, created = upsert_user_target(db, user["id"], name)
    db.commit()
    return {
        "id": target.id,
        "name": target.name,
        "created": created,
        "created_at": target.created_at,
        "updated_at": target.updated_at,
    }


async def save_ebsd_pair(
    request: Request,
    crc: Optional[UploadFile] = File(None),
    cpr: Optional[UploadFile] = File(None),
    ebsd: Optional[UploadFile] = File(None),
    sample: str = Form(...),
    position: str = Form(...),
    name: str = Form(""),
    folder: str = Form(""),
    version_key: Optional[str] = Form(None),
    version_label: Optional[str] = Form(None),
    version_num: Optional[int] = Form(None),
    db: Session = Depends(get_db),
):
    user = current_user(request)
    if ebsd is not None:
        filename = ebsd.filename or ""
        extension = os.path.splitext(filename)[1].lower()
        if extension not in {".ang", ".ctf"}:
            raise HTTPException(status_code=400, detail="single EBSD file must be .ang or .ctf")
        content, filename = await read_upload(ebsd, extension)
        crc_content = cpr_content = content
        crc_filename = cpr_filename = filename
        crc_extension = cpr_extension = extension
    elif crc is not None and cpr is not None:
        crc_content, crc_filename = await read_upload(crc, ".crc")
        cpr_content, cpr_filename = await read_upload(cpr, ".cpr")
        crc_extension, cpr_extension = ".crc", ".cpr"
    else:
        raise HTTPException(status_code=400, detail="upload .cpr + .crc, a single .ang, or a single .ctf file")

    if not sample.strip() or not position.strip():
        raise HTTPException(status_code=400, detail="sample and position are required")

    clean_sample, resolved_version_key, resolved_version_label, resolved_version_num = resolve_retest_version(
        sample,
        version_key.strip() if version_key else None,
        version_label.strip() if version_label else None,
        version_num,
    )

    target, _ = upsert_user_target(db, user["id"], clean_sample)
    crc_file, crc_created = get_or_create_ebsd_file(db, crc_content, crc_filename, crc_extension)
    cpr_file, cpr_created = get_or_create_ebsd_file(db, cpr_content, cpr_filename, cpr_extension)
    pair, pair_created = get_or_create_pair(db, crc_file, cpr_file)
    material = detect_material_from_metadata(
        cpp_material_metadata_from_files(crc_filename, crc_content, cpr_filename, cpr_content)
    )
    user_pair, user_pair_created = create_user_pair(
        db,
        user["id"],
        target,
        pair,
        display_name=name.strip() or f"{clean_sample}-{position}",
        position=position.strip(),
        folder=folder.strip() or None,
        version_key=resolved_version_key,
        version_label=resolved_version_label,
        version_num=resolved_version_num,
        material=material,
    )
    db.commit()

    return {
        "id": user_pair.id,
        "target_id": target.id,
        "pair_id": pair.id,
        "pair_hash": pair.pair_hash,
        "crc_hash": crc_file.content_hash,
        "cpr_hash": cpr_file.content_hash,
        "file_format": "cpr+crc" if cpr_extension == ".cpr" else cpr_extension.lstrip("."),
        "material": material,
        "created": {
            "crc_file": crc_created,
            "cpr_file": cpr_created,
            "pair": pair_created,
            "user_pair": user_pair_created,
        },
    }


def list_ebsd_pairs(request: Request, db: Session = Depends(get_db)):
    return list_ebsd_pair_metadata(request, db)


def list_ebsd_pair_metadata(request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
    rows = db.execute(
        _user_pair_metadata_query(user["id"])
        .order_by(UserEbsdPair.created_at.desc())
    ).scalars().all()

    return [serialize_user_pair_metadata(row) for row in rows]


def get_ebsd_pair_file(
    user_pair_id: str,
    kind: str,
    request: Request,
    db: Session = Depends(get_db),
):
    user = current_user(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")

    if kind == "primary":
        stored_file = row.pair.cpr_file
    elif kind == "crc":
        stored_file = row.pair.crc_file
    elif kind == "cpr":
        stored_file = row.pair.cpr_file
    else:
        raise HTTPException(status_code=400, detail="kind must be primary, crc, or cpr")

    return Response(
        content=stored_file.content,
        media_type="application/octet-stream",
        headers={
            "Content-Disposition": attachment_content_disposition(stored_file.original_filename),
            "X-File-Hash": stored_file.content_hash,
        },
    )


def delete_ebsd_pair(user_pair_id: str, request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
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


def delete_ebsd_target(target_name: str, request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
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


def validate_min_grain_size(value: Optional[int]) -> Optional[int]:
    if value is None:
        return None
    if value < 1 or value > 1000000:
        raise HTTPException(status_code=400, detail="min_grain_size must be between 1 and 1000000")
    return value


def normalize_reference_vector(value: Optional[List[float]]) -> Optional[List[float]]:
    if value is None:
        return None
    if len(value) != 3:
        raise HTTPException(status_code=400, detail="ipf_reference_vector must contain exactly 3 numbers")
    try:
        x, y, z = [float(v) for v in value]
    except (TypeError, ValueError):
        raise HTTPException(status_code=400, detail="ipf_reference_vector must contain finite numbers")
    norm = math.sqrt(x * x + y * y + z * z)
    if not math.isfinite(norm) or norm <= 1e-12:
        raise HTTPException(status_code=400, detail="ipf_reference_vector must not be zero")
    return [x / norm, y / norm, z / norm]


def validate_ipf_denoindex_threshold(value: Optional[float]) -> Optional[float]:
    if value is None:
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise HTTPException(status_code=400, detail="ipf_denoindex_threshold must be a number")
    if not math.isfinite(parsed) or parsed < 0 or parsed > 100:
        raise HTTPException(status_code=400, detail="ipf_denoindex_threshold must be between 0 and 100")
    return parsed


def validate_ipf_denoindex_misorientation_threshold(value: Optional[float]) -> Optional[float]:
    if value is None:
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise HTTPException(status_code=400, detail="ipf_denoindex_misorientation_threshold must be a number")
    if not math.isfinite(parsed) or parsed < 0 or parsed > 180:
        raise HTTPException(status_code=400, detail="ipf_denoindex_misorientation_threshold must be between 0 and 180")
    return parsed


def report_ipf_vectors(requested: Optional[List[float]]) -> Dict[str, List[float]]:
    """Always preserve the three standard PPT views; append a custom view."""
    vectors: Dict[str, List[float]] = {
        "X": [1.0, 0.0, 0.0],
        "Y": [0.0, 1.0, 0.0],
        "Z": [0.0, 0.0, 1.0],
    }
    custom_vector = normalize_reference_vector(requested)
    if custom_vector and not any(
        all(abs(custom_vector[index] - axis[index]) <= 1e-6 for index in range(3))
        for axis in vectors.values()
    ):
        vectors[
            f"Custom ({custom_vector[0]:.3g}, {custom_vector[1]:.3g}, {custom_vector[2]:.3g})"
        ] = custom_vector
    return vectors


def get_ebsd_pair_ipf_map(
    user_pair_id: str,
    request: Request,
    reference_x: Optional[float] = None,
    reference_y: Optional[float] = None,
    reference_z: Optional[float] = None,
    denoindex_threshold: Optional[float] = None,
    denoindex_misorientation_threshold: Optional[float] = None,
    include_scale_bar: bool = True,
    db: Session = Depends(get_db),
):
    user = current_user(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")

    reference_vector = None
    if reference_x is not None or reference_y is not None or reference_z is not None:
        if reference_x is None or reference_y is None or reference_z is None:
            raise HTTPException(status_code=400, detail="reference_x, reference_y, and reference_z are required together")
        reference_vector = normalize_reference_vector([reference_x, reference_y, reference_z])
    threshold = validate_ipf_denoindex_threshold(denoindex_threshold)
    misorientation_threshold = validate_ipf_denoindex_misorientation_threshold(denoindex_misorientation_threshold)

    return Response(
        content=cpp_ipf_map(row, reference_vector, threshold, misorientation_threshold, include_scale_bar),
        media_type="image/png",
    )


def get_ebsd_ipf_legend(request: Request):
    current_user(request)
    return Response(content=cpp_ipf_legend(), media_type="image/png")


def get_ebsd_pair_ipf_legend(user_pair_id: str, request: Request, db: Session = Depends(get_db)):
    user = current_user(request)
    row = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.id == user_pair_id,
            UserEbsdPair.user_id == user["id"],
        )
    ).scalar_one_or_none()
    if not row:
        raise HTTPException(status_code=404, detail="file pair not found")
    return Response(content=cpp_pair_ipf_legend(row), media_type="image/png")


