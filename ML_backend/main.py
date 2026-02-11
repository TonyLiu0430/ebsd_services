import os
import pathlib
import shutil
from typing import Annotated, Any, List
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
import defdap.ebsd as ebsd # type: ignore
import tempfile
import numpy as np
import joblib
from pydantic import BaseModel, TypeAdapter, ValidationError # type: ignore
from ebsd_utils import *

app = FastAPI()

@app.get('/')
def hello():
    return 'Hello world!'

def cal_feature(grain_size: list[int]):
    gs = np.array(grain_size)
    return np.array([gs.mean(), np.percentile(gs, 5), np.percentile(gs, 75), gs.max()])

def load_ml_model():
    model_filename = 'svm_model.joblib' 
    return joblib.load(model_filename) # type: ignore


#cpr crc : oxford binary ebsd file
@app.post('/predict')
async def ebsd_predict(crc_file: Annotated[UploadFile, File(...)], cpr_file: Annotated[UploadFile, File(...)]):
    if crc_file.filename is None or not crc_file.filename.endswith('.crc'):
        raise HTTPException(status_code=400, detail="missing crc file")
    if cpr_file.filename is None or not cpr_file.filename.endswith('.cpr'):
        raise HTTPException(status_code=400, detail="missing cpr file")

    with tempfile.TemporaryDirectory() as temp_dir:
        base_name = "input_data"
        crc_save_path = os.path.join(temp_dir, base_name + ".crc")
        cpr_save_path = os.path.join(temp_dir, base_name + ".cpr")

        with open(crc_save_path, "wb") as f:
            shutil.copyfileobj(crc_file.file, f)
        with open(cpr_save_path, "wb") as f:
            shutil.copyfileobj(cpr_file.file, f)

        ebsd_mp = ebsd.Map(crc_save_path)
        sizes = grain_sizes(ebsd_mp)
        clf = load_ml_model()
        feature = cal_feature(sizes)
        features_input = feature.reshape(1, -1)
        prediction = clf.predict(features_input)
        res = bool(prediction[0])
        return {
            "result": res
        }
    
class Ebsd_file_meta(BaseModel):
    sample: str
    pos: str

ebsd_meta_adapter = TypeAdapter(List[Ebsd_file_meta])

def ebsd_to_features(path: pathlib.Path):
    ebsd_map = ebsd.Map(str(path))
    grains = grain_sizes(ebsd_map)
    return [max(grains)]
    # return grains

@app.post('/all_data')  
def all_data(files: Annotated[List[UploadFile], File(...)], meta_data: Annotated[str, Form(...)]):
    try:
        metas = ebsd_meta_adapter.validate_json(meta_data)
    except ValidationError as e:
        raise HTTPException(status_code=422, detail=e.errors())
    
    if len(files) % 2 != 0:
        raise HTTPException(status_code=422, detail='crc cpr unpaired')
    
    it = iter(files)
    file_pairs = list(zip(it, it))

    res: Any = {}
    
    for (crc_file, cpr_file), meta in zip(file_pairs, metas):
        with tempfile.TemporaryDirectory() as temp_dir:
            base_name = "input_data"
            crc_save_path = os.path.join(temp_dir, base_name + ".crc")
            cpr_save_path = os.path.join(temp_dir, base_name + ".cpr")

            with open(crc_save_path, "wb") as f:
                shutil.copyfileobj(crc_file.file, f)
            with open(cpr_save_path, "wb") as f:
                shutil.copyfileobj(cpr_file.file, f)

            res.setdefault(meta.sample, {})[meta.pos] = ebsd_to_features(pathlib.Path(crc_save_path))

    return res
    





