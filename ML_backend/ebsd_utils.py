import numbers
import statistics
from typing import List
import defdap.ebsd as ebsd
from scipy.stats import wasserstein_distance # type: ignore
import numpy as np

def grain_sizes(mp: ebsd.Map):
    res = [len(grain) for grain in mp.grains] # type: ignore
    res.sort()
    return res

def eq_radius(step_size: float, pixel_area: float) -> float:
    return float(np.sqrt(pixel_area * step_size ** 2 / np.pi))

def Overall_distribution_analysis(golden: List[float], sample: List[float]) -> float:
    return float(wasserstein_distance(golden, sample)) / statistics.mean(golden)

# TODO TODO TODO TODO TODO TODO TODO

def mean_compare(golden: List[float], sample: List[float]) -> float:
    return numeric_compare(statistics.mean(golden), statistics.mean(sample))

# Standard Deviation
def sd_compare(golden: List[float], sample: List[float]) -> float:
    return numeric_compare(statistics.stdev(golden), statistics.stdev(sample))

def max_compare(golden: List[float], sample: List[float]) -> float:
    return numeric_compare(max(golden), max(sample))

def p25_compare(golden: List[float], sample: List[float]) -> float:
    return pN_compare(25, golden, sample)

def p75_compare(golden: List[float], sample: List[float]) -> float:
    return pN_compare(75, golden, sample)

def pN_compare(n: int, golden: List[float], sample: List[float]) -> float:
    return numeric_compare(percentiles(n, golden), percentiles(n, sample))

def numeric_compare(golden: float, sample: float) -> float:
    return abs(golden - sample) / golden

def percentiles(n: int, a: List[float]) -> float:
    return float(np.percentile(a, n))

