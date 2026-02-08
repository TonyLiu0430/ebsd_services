# type: ignore
# pyright: reportUnknownParameterType=false, reportUnknownMemberType=false
from sklearn.model_selection import *
from sklearn.metrics import *
from sklearn.svm import SVC
import numpy as np
import json
from pathlib import Path
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from joblib import dump

def get_ebsds(dir_path: str):
    dir = Path(dir_path)
    data = {}
    for subdir in dir.iterdir():
        data_patch, num = str(subdir).split('-')
        for path in subdir.iterdir():
            if num == '01':
                data[data_patch + '-' + path.name] = (path, True)
            else:
                data[data_patch + '-' + path.name + '-02'] = (path, True)
                ori_path, _ = data[data_patch + '-' + path.name]
                data[data_patch + '-' + path.name] = (ori_path, False)
    return data

def load_data():
    ebsd_sizes_table: dict
    with open(r"EBSD TEST DATA_20260121.secretdata") as f:
        ebsd_sizes_table = json.load(f)
    data = get_ebsds(r"E:\CODE_programming\.EBSD\EBSD TEST DATA_20260121")
    feature_vec = []
    class_vec = []
    for (path, accepted) in data.values():
        grain_sizes = ebsd_sizes_table[str(path)]
        feature_vec.append(get_feature(grain_sizes))
        class_vec.append(accepted)
    return (feature_vec, class_vec)

def get_feature(grain_size: list[int]):
    gs = np.array(grain_size)
    return np.array([gs.mean(), np.percentile(gs, 5), np.percentile(gs, 75), gs.max()])

def train(C: float, X, y):
    clf = make_pipeline(
        StandardScaler(),
        SVC(kernel='rbf', C=C, class_weight='balanced', gamma=1)
    )
    clf.fit(X, y)
    return clf



if __name__ == "__main__":
    X, y = load_data()
    clf = train(100, X, y)
    dump(clf, "svm_model.joblib")