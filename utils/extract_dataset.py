import os
import asyncio
import base64
import argparse
import json
import tempfile
from subprocess import call
import copy
from functools import partial

import aiohttp
from numpy import rec
import shortuuid
from dotenv import load_dotenv

from common import interpolate, klipper, last_value

load_dotenv()

RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"],
}

FRAMERATE = 20 # M5 Camera, VGA, roughly 20 FPS
DELAY = 0.6 # num of sec in which images lag behind the actual states

async def dump_images(begin, end, output_file):
    async with aiohttp.ClientSession() as session:
        with tempfile.TemporaryDirectory() as tmpdir:
            print("Saving images to", tmpdir)

            timestamps = []

            params = {
                "filter": json.dumps(
                    {
                        "$and": [
                            {"timestamp": {"$gte": begin}},
                            {"timestamp": {"$lte": end}},
                        ]
                    }
                ),
                "sort": "timestamp",
                "pagesize": 100,
            }
            page = 1
            index = 0
            while True:
                resp = await session.get(
                    f"{RESTHEART_ENDPOINT}/camera",
                    params={"page": page, **params},
                    headers=RESTHEART_HEADERS,
                    raise_for_status=True,
                )
                resp = await resp.json()
                if not resp:
                    print("end..")
                    break

                for rec in resp:
                    timestamps.append(rec["timestamp"])
                    bs = base64.b64decode(rec["image"])
                    fn = f"{tmpdir}/{index:04d}.jpg"
                    print(f"Saving file: {fn} @ {rec['timestamp']}")
                    with open(fn, "wb") as fp:
                        fp.write(bs)
                    index += 1

                page += 1

            call(
                (
                    f"ffmpeg -y -r {FRAMERATE} -i {tmpdir}/%04d.jpg "
                    f"-movflags +use_metadata_tags "
                    f"-metadata timestamp0={min(timestamps):.3f} {output_file}"
                ),
                shell=True,
            )

            print("Saving", output_file)
            return timestamps


async def iter_collection(coll, begin, end):
    async with aiohttp.ClientSession() as session:
        params = {
            "filter": json.dumps(
                {
                    "$and": [
                        {"timestamp": {"$gte": begin}},
                        {"timestamp": {"$lte": end}},
                    ]
                }
            ),
            "sort": "timestamp",
            "pagesize": 100,
        }
        page = 1
        index = 0

        while True:
            resp = await session.get(
                f"{RESTHEART_ENDPOINT}/{coll}",
                params={"page": page, **params},
                headers=RESTHEART_HEADERS,
                raise_for_status=True,
            )
            resp = await resp.json()
            if not resp:
                break

            for rec in resp:
                yield rec
                index += 1

            page += 1


async def get_arm_states(begin, end):
    print(f"Getting arm states between {begin} and {end}")
    records = []
    async for rec in iter_collection("robot", begin, end):
        records.append(
            {
                "timestamp": rec["timestamp"],
                "goal": rec["goal"],
                "position": rec["position"],
                "goal": rec["goal"],
                "velocity": rec["velocity"],
                "gripper": rec["gripper"],
            }
        )

    print(f"First state: {records[0]}, last state: {records[-1]}")
    return records


def down_sample(records, timestamps):
    if not timestamps:
        return []

    result = []
    ri = 0

    for ti in range(len(timestamps)):
        target = timestamps[ti]

        while ri < len(records) and records[ri]["timestamp"] < target:
            ri += 1

        # timestamp is beyond the last frame with known state
        if ri == len(records):
            result.extend(
                [
                    {"timestamp": t, "state": records[-1]["state"]}
                    for t in timestamps[ti:]
                ]
            )
            break

        # timestamp is before the first frame with known state
        if ri == 0:
            result.append({"timestamp": target, "state": records[0]["state"]})
            continue

        # match exactly
        if records[ri]["timestamp"] == target:
            result.append({"timestamp": target, "state": records[ri]["state"]})
            continue

        t0 = records[ri - 1]["timestamp"]
        t1 = records[ri]["timestamp"]
        s0 = records[ri - 1]["state"]
        s1 = records[ri]["state"]
        if s0 == s1:
            result.append({"timestamp": target, "state": s0})
            continue

        st = s0 + (s1 - s0) * (target - t0) / (t1 - t0)
        result.append({"timestamp": target, "state": st})

    for rec, nxt in zip(result, result[1:]):
        rec["goal"] = nxt["state"]

    result[-1]["goal"] = result[-1]["state"]

    return result


def assign_arm_goals(interpolated, original):
    len_inter = len(interpolated)
    j = 0

    for i in range(len(original)):
        while j < len_inter and interpolated[j]["timestamp"] < original[i]["timestamp"]:
            interpolated[j]["goal"] = interpolated[j]["position"].copy()
            j += 1

        if j >= len_inter:
            print("Some original records have to be discarded.")
            break

        interpolated[j]["goal"] = original[i]["goal"].copy()


def assign_goal_from_next(records, *, key):
    for i in range(len(records) - 1):
        records[i]["goal"] = copy.copy(records[i + 1][key])

    records[-1]["goal"] = copy.copy(records[-1][key])


def merge(arm_records, gripper_records):
    results = []
    for ar, gr in zip(arm_records, gripper_records):
        assert (
            ar["timestamp"] == gr["timestamp"]
        ), f"{ar['timestamp']} vs {gr['timestamp']}"
        rec = copy.deepcopy(ar)
        rec["position"].append(gr["state"])
        rec["goal"].append(gr["goal"])
        results.append(rec)

    return results


def main():
    parser = argparse.ArgumentParser(prog="execute")
    parser.add_argument("--from-path", required=False)
    parser.add_argument("-o", "--output", default="output")
    parser.add_argument("begin", type=float)
    parser.add_argument("end", type=float)
    args = parser.parse_args()

    name = shortuuid.uuid()

    async def _ep():

        # grab a little more data because it is by 0.25s interval
        arm_records = await get_arm_states(args.begin - 0.5, args.end + 1.5)

        arm_timestamps = [r["timestamp"] for r in arm_records]
        print(f"Saving states between {arm_timestamps[0]} and {arm_timestamps[-1]}")
        timestamps = await dump_images(
            arm_timestamps[0] + DELAY, args.end + 1, f"{args.output}/{name}.mp4"
        )
        print(timestamps[:5])
        timestamps = [t - DELAY for t in timestamps]

        # assert timestamps == final_timestamps

        rem1, arm_positions = interpolate(
            timestamps,
            arm_records,
            interpolate_func=klipper,
        )
        rem2, gripper_positions = interpolate(
            timestamps,
            arm_records,
            interpolate_func=partial(last_value, valname="gripper"),
        )
        if len(rem1 + rem2) != 0:
            print(f"some records of arm left over: {rem1} {rem2}")

        arm_positions = [
            {"timestamp": t, "position": p} for t, p in zip(timestamps, arm_positions)
        ]
        gripper_positions = [
            {"timestamp": t, "state": p} for t, p in zip(timestamps, gripper_positions)
        ]
        assign_goal_from_next(arm_positions, key="position")
        assign_goal_from_next(gripper_positions, key="state")

        assert len(arm_positions) == len(
            gripper_positions
        ), f"{len(arm_positions)} vs {len(gripper_positions)}"

        # print(arm_positions[:5])
        # print(gripper_records[:5])

        obj = {
            "begin": args.begin,
            "end": args.end,
            "video_time_begin": min(timestamps),
            "video_time_end": max(timestamps),
            "states": merge(arm_positions, gripper_positions),
        }

        if path_file := args.from_path:
            obj["path_file"] = path_file

        output_file = f"{args.output}/{name}.json"
        with open(output_file, "w") as fp:
            json.dump(obj, fp, indent=2)

    asyncio.run(_ep())
    print("output name:", name)


if __name__ == "__main__":
    main()
