import inspect
import os
import pathlib
import shutil
from typing import Annotated, Any, Dict, List
from fastapi import FastAPI, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import JSONResponse
import defdap.ebsd as ebsd # type: ignore
import tempfile
import numpy as np
import joblib
from pydantic import BaseModel, TypeAdapter, ValidationError # type: ignore
from ebsd_utils import *
from typing import Callable, Any
from auth import AUTH_PASSWORD, AUTH_USERNAME, JWT_EXPIRES_IN, create_token, parse_bearer_token, verify_token

app = FastAPI()

@app.get('/')
def hello():
    return 'Hello world!'


PUBLIC_PATHS = {
    '/',
    '/login',
    '/docs',
    '/openapi.json',
    '/redoc',
}


@app.middleware("http")
async def jwt_auth_middleware(request: Request, call_next):
    if request.method == "OPTIONS" or request.url.path in PUBLIC_PATHS:
        return await call_next(request)

    try:
        token = parse_bearer_token(request.headers.get("Authorization"))
        request.state.jwt_payload = verify_token(token)
    except ValueError as exc:
        return JSONResponse(status_code=401, content={"message": str(exc)})

    return await call_next(request)

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

class AnalysisRequest(BaseModel):
    features: List[str]
    golden: str
    # payload (from cpp_backend features + frontend result):
    # sample -> pos -> {"grains": [...], "orientation_ratio(20%)": [..3..], "orientation_ratio(15%)": [..3..]}
    data: Dict[str, Dict[str, Dict[str, Any]]]


class LoginRequest(BaseModel):
    username: str
    password: str


@app.post('/login')
def login(req: LoginRequest):
    if req.username != AUTH_USERNAME or req.password != AUTH_PASSWORD:
        raise HTTPException(status_code=401, detail="Invalid username or password")

    return {
        "token": create_token(req.username),
        "token_type": "Bearer",
        "expires_in": JWT_EXPIRES_IN,
    }


@app.get('/auth/verify')
def auth_verify(request: Request):
    return {
        "valid": True,
        "payload": request.state.jwt_payload,
    }


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
            statRes: List[float] = []
            for feat in req.features:
                if feat in GRAIN_FEATURES:
                    handler = GRAIN_FEATURES[feat]
                    statRes.append(handler(req.data[req.golden][pos]["grains"], cur_data["grains"]))
                elif feat in ORIENTATION_FEATURES:
                    handler = ORIENTATION_FEATURES[feat]
                    # feat example: "[001] (deviation 20%)"
                    try:
                        axis = feat.split(']')[0].lstrip('[')  # 001 / 110 / 111
                        deviation = "20" if "20%" in feat else "15"
                        ratio_key = f"orientation_ratio({deviation}%)"
                        idx_map = {"001": 0, "110": 1, "111": 2}
                        idx = idx_map[axis]
                        golden_ratio = req.data[req.golden][pos][ratio_key]
                        sample_ratio = cur_data[ratio_key]
                        statRes.append(handler(float(golden_ratio[idx]), float(sample_ratio[idx])))
                    except Exception:
                        raise HTTPException(status_code=400, detail=f"Invalid orientation feature/data: {feat}")

            # TODO 改用 Winsorized mean 並且前端要可調整權重
            if len(statRes) > 1:
                statRes = [abs(x) for x in statRes]
            avg = statistics.mean(statRes)
            res.setdefault(sample, {})[pos] = avg
    return res
    # for feat in req.features:
    #     if feat not in GRAIN_FEATURES:
    #         raise HTTPException(status_code=400, detail="Invalid feature options")
    # if req.golden not in req.data:
    #     raise HTTPException(status_code=400, detail="Invalid golden sample")
    # res: Any = {}
    # for (sample, posdata) in req.data.items():
    #     if sample == req.golden:
    #         continue
    #     for (pos, grain_data) in posdata.items():
    #         statRes: List[float] = []
    #         for feat in req.features:
    #             handler = GRAIN_FEATURES[feat]
    #             statRes.append(handler(req.data[req.golden][pos], grain_data))

    #         # TODO 改用 Winsorized mean 並且前端要可調整權重
    #         avg = statistics.mean(statRes)
    #         res.setdefault(sample, {})[pos] = avg
    # return res

            

    

        






