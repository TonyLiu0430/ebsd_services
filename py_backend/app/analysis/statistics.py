import statistics
from typing import List
from scipy.stats import wasserstein_distance # type: ignore
import numpy as np

def Overall_distribution_analysis(golden: List[float], sample: List[float]) -> float:
    return float(wasserstein_distance(golden, sample)) / statistics.mean(golden)

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
    return (sample - golden) / golden

def percentiles(n: int, a: List[float]) -> float:
    return float(np.percentile(a, n)) # type: ignore
