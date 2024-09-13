import argparse
import sys
import os
import json
import asyncio
import time
import base64
import tempfile
from subprocess import check_call

import shortuuid
import aiomqtt
from websockets.asyncio.client import connect as ws_connect
from dotenv import load_dotenv
import aiohttp

load_dotenv()

MQTT_SERVER_HOST = os.environ["MQTT_SERVER_HOST"]
MQTT_SERVER_PORT = os.environ.get("MQTT_SERVER_PORT", 1883)
MOONRAKER_URL = os.environ["MOONRAKER_URL"]
RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"]
}


async def run_gcode(ws, gcode):
    await ws.send(
        json.dumps({
            "jsonrpc": "2.0",
            "method": "printer.gcode.script",
            "params": {
                "script": gcode + "\nM400"
            },
            "id": 200
        })
    )

    while True:
        resp = await ws.recv()
        resp = json.loads(resp)
        if "result" in resp:
            print(resp)
            break


async def dump_images(begin, end, output_file):
    async with aiohttp.ClientSession() as session:
        with tempfile.TemporaryDirectory() as tmpdir:
            print("Saving images to", tmpdir)

            params = {
                "filter": json.dumps({"$and": [
                    {"timestamp": {"$gte": begin}},
                    {"timestamp": {"$lte": end}},
                ]}),
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
                    bs = base64.b64decode(rec["image"])
                    fn = f"{tmpdir}/{index:04d}.jpg"
                    print("Saving file: ", fn)
                    with open(fn, "wb") as fp:
                        fp.write(bs)
                    index += 1

                page += 1

            check_call(
                f"ffmpeg -y -r 10 -i {tmpdir}/%04d.jpg {output_file}",
                shell=True
            )


async def dump_states(begin, end, output_file):
    async with aiohttp.ClientSession() as session:
        params = {
            "filter": json.dumps({"$and": [
                {"timestamp": {"$gte": begin}},
                {"timestamp": {"$lte": end}},
            ]}),
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
                records.append({
                    "timestamp": rec["timestamp"],
                    "goal": rec["goal"],
                    "position": rec["position"],
                })
                index += 1

            page += 1

        with open(output_file, "w") as fp:
            json.dump(records, fp)


async def execute(path, args):
    begin = time.time()
    async with aiomqtt.Client(MQTT_SERVER_HOST, port=MQTT_SERVER_PORT) as mqtt:
        async with ws_connect(MOONRAKER_URL) as ws:
            for s in path:
                pos = s["positions"]
                gcode = "G1 " + " ".join([n+str(v) for n,v in zip("XYZABC", pos)])
                await run_gcode(ws, gcode)
                print(gcode)
                gr = s["gripper"]
                await mqtt.publish("/manipulator/command", gr)
                print(gr)
                await asyncio.sleep(0.5)
    end = time.time()

    print("begin:", begin, " end:", end)

    name = shortuuid.uuid()

    with open(f"{args.output}/{name}.json") as fp:
        json.dump(
            {
                "pathfile": args.pathfile,
                "timestamp_begin": begin,
                "timestamp_end": end,
            },
            fp
        )

    await dump_images(begin, end, f"{args.output}/videos/{name}.mp4")
    await dump_states(begin, end+1, f"{args.output}/{name}_states.json")


def main():
    parser = argparse.ArgumentParser(prog="execute")
    parser.add_argument("pathfile")
    parser.add_argument("-o", "--output", default="export")
    args = parser.parse_args()

    with open(args.pathfile, "r") as fp:
        path = json.load(fp)

    if isinstance(path, dict):
        # single step
        path = [path]

    assert isinstance(path, list)
    asyncio.run(execute(path, args))


def debug():
    #begin = 1726174782
    #end = 1726174798
    begin = 1726262488
    end = 1726262510
    #asyncio.run(dump_images(begin, end, "output.mp4"))
    asyncio.run(dump_states(begin, end, "output.json"))


if __name__ == "__main__":
    #main()
    debug()
