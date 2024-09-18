import os
import asyncio
import base64
import argparse
import json
import tempfile
from subprocess import call

import aiohttp
import shortuuid
from dotenv import load_dotenv


load_dotenv()

RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"],
}


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
                    print("Saving file: ", fn)
                    with open(fn, "wb") as fp:
                        fp.write(bs)
                    index += 1

                page += 1

            call(
                (
                    f"ffmpeg -y -r 10 -i {tmpdir}/%04d.jpg "
                    f"-movflags +use_metadata_tags "
                    f"-metadata timestamp0={min(timestamps):.3f} {output_file}"
                ),
                shell=True,
            )

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
    records = []
    async for rec in iter_collection("robot", begin, end):
        records.append({
            "timestamp": rec["timestamp"],
            "goal": rec["goal"],
            "position": rec["position"],
            "goal": rec["goal"],
            "velocity": rec["velocity"],
        })

    return records


async def get_gripper_states(begin, end):
    records = []
    async for rec in iter_collection("gripper", begin, end):
        records.append({
            "timestamp": rec["timestamp"],
            "state": rec["state"],
        })
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
            result.extend([
                {"timestamp": t, "state": records[-1]["state"]}
                for t in timestamps[ti:]
            ])
            break

        # timestamp is before the first frame with known state
        if ri == 0:
            result.append({"timestamp": target, "state": records[0]["state"]})
            continue

        # match exactly
        if records[ri]["timestamp"] == target:
            result.append({"timestamp": target, "state": records[ri]["state"]})
            continue

        t0 = records[ri-1]["timestamp"]
        t1 = records[ri]["timestamp"]
        s0 = records[ri-1]["state"]
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


def merge(arm_records, gripper_records):
    results = []
    for ar, gr in zip(arm_records, gripper_records):
        assert ar["timestamp"] == gr["timestamp"], f"{ar['timestamp']} vs {gr['timestamp']}"
        rec = ar.copy()
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
        timestamps = await dump_images(
            args.begin, args.end+1, f"{args.output}/{name}.mp4"
        )

        arm_records = await get_arm_states(args.begin, args.end + 1)
        gripper_records = await get_gripper_states(args.begin, args.end + 1)
        gripper_records = down_sample(gripper_records, [r["timestamp"] for r in arm_records])

        assert len(arm_records) == len(gripper_records), f"{len(arm_records)} vs {len(gripper_records)}"

        print(arm_records[:5])
        print(gripper_records[:5])

        obj = {
            "begin": args.begin,
            "end": args.end,
            "video_time_begin": min(timestamps),
            "video_time_end": max(timestamps),
            "states": merge(arm_records, gripper_records),
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
