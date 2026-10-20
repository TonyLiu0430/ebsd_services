"""EBSD library, files, and map endpoints."""

from typing import Optional

from fastapi import APIRouter, Depends, File, Form, Request, UploadFile
from sqlalchemy.orm import Session

from app.db.store import get_db
from app.services import ebsd as ebsd_service


router = APIRouter()


@router.get("/ebsd_features")
def ebsd_features():
    return ebsd_service.ebsd_features()


@router.get("/ebsd/targets")
def list_ebsd_targets(request: Request, db: Session = Depends(get_db)):
    return ebsd_service.list_ebsd_targets(request, db)


@router.get("/ebsd/library/summary")
def get_ebsd_library_summary(request: Request, db: Session = Depends(get_db)):
    return ebsd_service.get_ebsd_library_summary(request, db)


@router.post("/ebsd/targets")
async def create_ebsd_target(
    request: Request, name: str = Form(...), db: Session = Depends(get_db)
):
    return await ebsd_service.create_ebsd_target(request, name, db)


@router.post("/ebsd/pairs")
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
    return await ebsd_service.save_ebsd_pair(
        request, crc, cpr, ebsd, sample, position, name, folder,
        version_key, version_label, version_num, db,
    )


@router.get("/ebsd/pairs")
def list_ebsd_pairs(request: Request, db: Session = Depends(get_db)):
    return ebsd_service.list_ebsd_pairs(request, db)


@router.get("/ebsd/pairs/metadata")
def list_ebsd_pair_metadata(request: Request, db: Session = Depends(get_db)):
    return ebsd_service.list_ebsd_pair_metadata(request, db)


@router.get("/ebsd/pairs/{user_pair_id}/files/{kind}")
def get_ebsd_pair_file(
    user_pair_id: str, kind: str, request: Request, db: Session = Depends(get_db)
):
    return ebsd_service.get_ebsd_pair_file(user_pair_id, kind, request, db)


@router.delete("/ebsd/pairs/{user_pair_id}")
def delete_ebsd_pair(
    user_pair_id: str, request: Request, db: Session = Depends(get_db)
):
    return ebsd_service.delete_ebsd_pair(user_pair_id, request, db)


@router.delete("/ebsd/targets/{target_name}")
def delete_ebsd_target(
    target_name: str, request: Request, db: Session = Depends(get_db)
):
    return ebsd_service.delete_ebsd_target(target_name, request, db)


@router.get("/ebsd/pairs/{user_pair_id}/ipf_map")
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
    return ebsd_service.get_ebsd_pair_ipf_map(
        user_pair_id, request, reference_x, reference_y, reference_z,
        denoindex_threshold, denoindex_misorientation_threshold,
        include_scale_bar, db,
    )


@router.get("/ebsd/ipf_legend")
def get_ebsd_ipf_legend(request: Request):
    return ebsd_service.get_ebsd_ipf_legend(request)


@router.get("/ebsd/pairs/{user_pair_id}/ipf_legend")
def get_ebsd_pair_ipf_legend(
    user_pair_id: str, request: Request, db: Session = Depends(get_db)
):
    return ebsd_service.get_ebsd_pair_ipf_legend(user_pair_id, request, db)
