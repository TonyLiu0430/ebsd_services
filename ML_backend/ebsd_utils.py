import numbers
import statistics
from typing import List
import defdap.ebsd as ebsd
from scipy.stats import wasserstein_distance
import numpy as np

def grain_sizes(mp: ebsd.Map):
    res = [len(grain) for grain in mp.grains] # type: ignore
    res.sort()
    return res

def Overall_distribution_analysis(golden: List[int], sample: List[int]) -> float:
    return float(wasserstein_distance(golden, sample)) / statistics.mean(golden)

# TODO TODO TODO TODO TODO TODO TODO

def mean_compare(golden: List[int], sample: List[int]) -> float:
    return numeric_compare(statistics.mean(golden), statistics.mean(sample))

def max_compare(golden: List[int], sample: List[int]) -> float:
    return numeric_compare(max(golden), max(sample))

def p25_compare(golden: List[int], sample: List[int]) -> float:
    return pN_compare(25, golden, sample)

def p75_compare(golden: List[int], sample: List[int]) -> float:
    return pN_compare(75, golden, sample)

def pN_compare(n: int, golden: List[int], sample: List[int]) -> float:
    return numeric_compare(percentiles(n, golden), percentiles(n, sample))

def numeric_compare(golden: float, sample: float) -> float:
    return abs(golden - sample) / golden

def percentiles(n: int, a: List[int]) -> float:
    if not a:
        return 0
    sorted_a = sorted(a)
    length = len(sorted_a)
    index = (n / 100.0) * (length - 1)
    lower_index = int(index)
    upper_index = lower_index + 1
    
    if upper_index >= length:
        return float(sorted_a[lower_index])
    
    lower_value = sorted_a[lower_index]
    upper_value = sorted_a[upper_index]
    fraction = index - lower_index
    return lower_value + fraction * (upper_value - lower_value)

