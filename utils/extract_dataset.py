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


async def dump_states(begin, end, output_file, path_file=None, extra=None):
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
        records = []

        while True:
            resp = await session.get(
                f"{RESTHEART_ENDPOINT}/robot",
                params={"page": page, **params},
                headers=RESTHEART_HEADERS,
                raise_for_status=True,
            )
            resp = await resp.json()
            if not resp:
                print("end..")
                break

            for rec in resp:
                records.append(
                    {
                        "timestamp": rec["timestamp"],
                        "goal": rec["goal"],
                        "position": rec["position"],
                        "goal": rec["goal"],
                        "velocity": rec["velocity"],
                    }
                )
                index += 1

            page += 1

        with open(output_file, "w") as fp:
            obj = {
                "begin": begin,
                "end": end,
                "states": records,
                **(extra or {}),
            }

            if path_file:
                obj["path_file"] = path_file

            json.dump(obj, fp, indent=2)


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
        await dump_states(
            args.begin,
            args.end + 1,
            f"{args.output}/{name}.json",
            path_file=args.from_path,
            extra={
                "video_time_begin": min(timestamps),
                "video_time_end": max(timestamps),
            },
        )

    asyncio.run(_ep())
    print("output name:", name)


if __name__ == "__main__":
    main()
