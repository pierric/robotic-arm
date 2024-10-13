from typing import List, Dict

import numpy as np

def linear(s, t0, rec0, t1, rec1, *, valname):
    v0 = rec0[valname]
    v1 = rec1[valname]

    if s == t0:
        return v0

    fr = (s - t0) / (t1 - t0)

    if isinstance(v0, (int, float)):
        return v0 * (1 -fr) + v1 * fr
    elif isinstance(v0, list):
        assert isinstance(v1, list)
        assert len(v0) == len(v1), f"vector of different size: {v0} {v1}"
        assert isinstance(v0[0], (int, float))
        return [
            va * (1 -fr) + vb * fr
            for va, vb in zip(v0, v1)
        ]

    raise RuntimeError("Unable to process the values")


def last_value(s, t0, rec0, t1, rec1, *, valname):
    return rec0[valname]


def klipper(s, t0, rec0, t1, rec1):
    p0 = np.array(rec0["position"])
    v0 = rec0["velocity"]
    p1 = np.array(rec1["position"])
    v1 = rec1["velocity"]

    if s == t0 or t0 == t1:
        return p0.tolist()

    if v0 == v1 and v0 == 0:
        return p0.tolist()

    assert t0 <= s <= t1

    # acceleration, if 0 (because of v0 == v1), then the same as a linear interpolation
    acc = (v1 - v0) / (t1 - t0)

    # d is the whole traveling distance
    # r is the ratio of each component w.r.t the whole traveling distance
    t = t1 - t0
    d = v0 * t + 0.5 * acc * t * t
    r = (p1 - p0) / d

    # calcuate the position at time s
    dt = s - t0
    dd = v0 * dt + 0.5 * acc * dt * dt
    return (p0 + r * dd).tolist()


def interpolate(steps: List[float], key_points: List[Dict], *, keyname="timestamp", interpolate_func):

    assert key_points[0][keyname] <= steps[0], f"precondition not met: {key_points[0][keyname]} <= {steps[0]}"

    ret = []
    ki = 0

    for si, s in enumerate(steps):

        while ki < len(key_points) and key_points[ki][keyname] < s:
            ki += 1

        if ki == len(key_points):
            # not enough key points
            return steps[si:], ret

        if ki == 0:
            r = key_points[ki]
            t = r[keyname]
            ret.append(interpolate_func(t, t, r, t, r))
            continue

        ki -= 1

        t0 = key_points[ki][keyname]
        t1 = key_points[ki+1][keyname]
        val = interpolate_func(s, t0, key_points[ki], t1, key_points[ki+1])
        ret.append(val)

    return [], ret
