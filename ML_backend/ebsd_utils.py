import defdap.ebsd as ebsd

def grain_sizes(mp: ebsd.Map):
    res = [len(grain) for grain in mp.grains] # type: ignore
    res.sort()
    return res