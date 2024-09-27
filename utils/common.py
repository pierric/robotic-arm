from typing import List, Dict


def interpolate(steps: List[float], key_points: List[Dict], keyname="timestamp", valname="state"):

    assert key_points[0][keyname] < steps[0], "precondition not met"

    ret = []
    ki = 0

    for si, s in enumerate(steps):

        while ki < len(key_points) and key_points[ki][keyname] < s:
            ki += 1

        if ki == len(key_points):
            # not enough key points
            return steps[si:], ret

        ki -= 1

        t0 = key_points[ki][keyname]
        t1 = key_points[ki+1][keyname]
        fr = (s - t0) / (t1 - t0)

        v0 = key_points[ki][valname]
        v1 = key_points[ki+1][valname]

        if isinstance(v0, (int, float)):
            val = v0 * (1 -fr) + v1 * fr
        elif isinstance(v0, list):
            assert isinstance(v1, list)
            assert len(v0) == len(v1)
            assert isinstance(v0[0], (int, float))
            val = [
                va * (1 -fr) + vb * fr
                for va, vb in zip(v0, v1)
            ]
        else:
            raise RuntimeError("Unable to process the values")

        ret.append(val)

    return [], ret
