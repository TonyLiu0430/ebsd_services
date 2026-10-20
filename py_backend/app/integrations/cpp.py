"""HTTP client for the C++ EBSD processing service."""

import json
import os
import urllib.error
import urllib.request
import uuid
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import urlencode

from fastapi import HTTPException

from app.analysis.figures import RawRgbMap
from app.db.store import UserEbsdPair


CPP_BACKEND_URL = os.getenv("CPP_BACKEND_URL", "http://cpp_backend:8080").rstrip("/")
CPP_BACKEND_TIMEOUT_SECONDS = int(os.getenv("CPP_BACKEND_TIMEOUT_SECONDS", "180"))


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


def post_cpp_multipart(
    endpoint: str,
    parts: List[Tuple[str, str, str, bytes]],
    accept: str,
    query_params: Optional[Dict[str, Any]] = None,
) -> bytes:
    body, boundary = multipart_body(parts)
    last_error: Optional[Exception] = None
    for base_url in cpp_backend_candidates():
        url = f"{base_url}{endpoint}"
        if query_params:
            url = f"{url}?{urlencode(query_params)}"
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


def post_cpp_files(
    endpoint: str,
    *,
    crc_filename: str,
    crc_content: bytes,
    cpr_filename: str,
    cpr_content: bytes,
    accept: str,
    min_grain_size: Optional[int] = None,
    ipf_reference_vector: Optional[List[float]] = None,
    ipf_denoindex_threshold: Optional[float] = None,
    ipf_denoindex_misorientation_threshold: Optional[float] = None,
    include_scale_bar: Optional[bool] = None,
) -> bytes:
    query_params: Dict[str, Any] = {}
    if min_grain_size is not None:
        query_params["min_grain_size"] = min_grain_size
    if ipf_reference_vector is not None:
        query_params.update({
            "reference_x": ipf_reference_vector[0],
            "reference_y": ipf_reference_vector[1],
            "reference_z": ipf_reference_vector[2],
        })
    if ipf_denoindex_threshold is not None:
        query_params["denoindex_threshold"] = ipf_denoindex_threshold
    if ipf_denoindex_misorientation_threshold is not None:
        query_params["denoindex_misorientation_threshold"] = ipf_denoindex_misorientation_threshold
    if include_scale_bar is not None:
        query_params["include_scale_bar"] = str(include_scale_bar).lower()
    return post_cpp_multipart(
        endpoint,
        [
            ("crc", crc_filename, "application/octet-stream", crc_content),
            ("cpr", cpr_filename, "application/octet-stream", cpr_content),
        ],
        accept,
        query_params,
    )


def post_cpp_pair(
    endpoint: str,
    row: UserEbsdPair,
    accept: str,
    min_grain_size: Optional[int] = None,
    ipf_reference_vector: Optional[List[float]] = None,
    ipf_denoindex_threshold: Optional[float] = None,
    ipf_denoindex_misorientation_threshold: Optional[float] = None,
    include_scale_bar: Optional[bool] = None,
) -> bytes:
    return post_cpp_files(
        endpoint,
        crc_filename=row.pair.crc_file.original_filename,
        crc_content=row.pair.crc_file.content,
        cpr_filename=row.pair.cpr_file.original_filename,
        cpr_content=row.pair.cpr_file.content,
        accept=accept,
        min_grain_size=min_grain_size,
        ipf_reference_vector=ipf_reference_vector,
        ipf_denoindex_threshold=ipf_denoindex_threshold,
        ipf_denoindex_misorientation_threshold=ipf_denoindex_misorientation_threshold,
        include_scale_bar=include_scale_bar,
    )


def get_cpp_bytes(endpoint: str, accept: str = "application/octet-stream") -> bytes:
    last_error: Optional[Exception] = None
    for base_url in cpp_backend_candidates():
        url = f"{base_url}{endpoint}"
        req = urllib.request.Request(url, headers={"Accept": accept}, method="GET")
        try:
            with urllib.request.urlopen(req, timeout=CPP_BACKEND_TIMEOUT_SECONDS) as res:
                return res.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise HTTPException(status_code=502, detail=f"C++ backend {endpoint} failed: {detail}")
        except Exception as exc:
            last_error = exc
    raise HTTPException(status_code=502, detail=f"C++ backend unavailable: {last_error}")


def cpp_material_metadata_from_files(crc_filename: str, crc_content: bytes, cpr_filename: str, cpr_content: bytes) -> Dict[str, Any]:
    raw = post_cpp_files(
        "/material_metadata",
        crc_filename=crc_filename,
        crc_content=crc_content,
        cpr_filename=cpr_filename,
        cpr_content=cpr_content,
        accept="application/json",
    )
    try:
        data = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=502, detail=f"C++ backend returned invalid material metadata JSON: {exc}")
    if not isinstance(data, dict):
        raise HTTPException(status_code=502, detail="C++ backend returned invalid material metadata payload")
    return data


def cpp_features(row: UserEbsdPair, min_grain_size: Optional[int] = None) -> Dict[str, Any]:
    raw = post_cpp_pair("/features", row, "application/json", min_grain_size)
    try:
        data = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=502, detail=f"C++ backend returned invalid feature JSON: {exc}")
    if not isinstance(data, dict):
        raise HTTPException(status_code=502, detail="C++ backend returned invalid feature payload")
    return data


def cpp_ipf_map(
    row: UserEbsdPair,
    ipf_reference_vector: Optional[List[float]] = None,
    ipf_denoindex_threshold: Optional[float] = None,
    ipf_denoindex_misorientation_threshold: Optional[float] = None,
    include_scale_bar: bool = True,
) -> bytes:
    return post_cpp_pair(
        "/ipf_map",
        row,
        "image/png",
        ipf_reference_vector=ipf_reference_vector,
        ipf_denoindex_threshold=ipf_denoindex_threshold,
        ipf_denoindex_misorientation_threshold=ipf_denoindex_misorientation_threshold,
        include_scale_bar=include_scale_bar,
    )


def cpp_band_contrast_map(row: UserEbsdPair, include_scale_bar: bool = True) -> bytes:
    return post_cpp_pair(
        "/band_contrast_map",
        row,
        "image/png",
        include_scale_bar=include_scale_bar,
    )


def cpp_pole_figure(row: UserEbsdPair) -> bytes:
    return post_cpp_pair("/pole_figure", row, "image/png")


def cpp_inverse_pole_figure(row: UserEbsdPair) -> bytes:
    return post_cpp_pair("/inverse_pole_figure", row, "image/png")


def cpp_scale_bar(image: RawRgbMap) -> bytes:
    return post_cpp_multipart(
        "/scale_bar",
        [("image", f"{image.map_name}.rgb", "application/octet-stream", image.pixels)],
        "image/png",
        {
            "width": image.width,
            "height": image.height,
            "x_step_um": image.x_step_um,
            "map_name": image.map_name,
        },
    )


def cpp_ipf_legend() -> bytes:
    return get_cpp_bytes("/ipf_legend", "image/png")


def cpp_pair_ipf_legend(row: UserEbsdPair) -> bytes:
    return post_cpp_pair("/ipf_legend", row, "image/png")


