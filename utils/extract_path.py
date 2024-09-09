import os
import json
from typing import List, Dict
import argparse
from functools import partial

import requests
from dotenv import load_dotenv

load_dotenv()

RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"]
}

def query_path(name):
    params = {
        "filter": json.dumps({"name": name}),
    }
    resp = requests.get(f"{RESTHEART_ENDPOINT}/paths", params, headers=RESTHEART_HEADERS)
    resp.raise_for_status()
    return resp.json()[0]["steps"]


def query_mongo(collection, begin, end):
    qobj = {
        "$and": [
            {"timestamp": {"$gte": begin-1} },
            {"timestamp": {"$lte": end+1} },
        ]
    }

    params = {
        "filter": json.dumps(qobj),
        "sort": "timestamp",
        "pagesize": 100,
    }

    resp = requests.get(
        f"{RESTHEART_ENDPOINT}/{collection}",
        params,
        headers=RESTHEART_HEADERS
    )
    resp.raise_for_status()
    return resp.json()


def query_and_match(steps, query_func, *, keyname="timestamp", valname):

    prfx = []
    ret = []

    while True:
        records = query_func(steps[0], steps[-1])
        if not records:
            # probably should be an error instead
            print("no more records found")
            break

        # records may contains partial results, matching only the head of steps
        steps, matched = match(
            steps,
            [project(r, [keyname, valname]) for r in prfx + records],
            keyname=keyname,
            valname=valname,
        )

        ret.extend(matched)

        if not steps:
            break

        prfx = [records[-1]]

    return ret


def project(dict, keys):
    return {
        k: dict[k]
        for k in keys
    }


def match(steps: List[float], key_points: List[Dict], keyname="timestamp", valname="state"):

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


def main():
    parser = argparse.ArgumentParser(prog="extract a path")
    parser.add_argument("--name", required=True)
    args = parser.parse_args()

    steps = query_path(args.name)

    gstates = query_and_match(steps, partial(query_mongo, "gripper"), valname="state")
    rstates = query_and_match(steps, partial(query_mongo, "robot"), valname="positions")
    res = [{"gripper": g, "positions": r} for g, r in zip(gstates, rstates)]

    print(json.dumps(res))


if __name__ == "__main__":
    main()
