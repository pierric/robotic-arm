import argparse
import os
import json
import asyncio
import time

import aiomqtt
from websockets.asyncio.client import connect as ws_connect
from dotenv import load_dotenv

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
    print("gcode sent.")

    while True:
        resp = await ws.recv()
        resp = json.loads(resp)
        if "result" in resp:
            break
        if "error" in resp:
            print(resp)
            break


async def execute(path, args, start_record_at=1):
    async with aiomqtt.Client(MQTT_SERVER_HOST, port=MQTT_SERVER_PORT) as mqtt:
        async with ws_connect(MOONRAKER_URL) as ws:

            if isinstance(args.move_to, int):
                s = path[args.move_to]
                pos = s["positions"]
                gcode = "G1 " + " ".join([f"{n}{v:.3f}" for n,v in zip("XYZABC", pos)])
                await run_gcode(ws, gcode)
                gr = s["gripper"]
                await mqtt.publish("/manipulator/command", gr)
                return

            begin = time.time()

            for i, s in enumerate(path):
                if i == start_record_at:
                    begin = time.time()
                    await mqtt.publish("/camera/command", "on")
                    await asyncio.sleep(0.5)

                print(s)
                pos = s["positions"]
                gcode = "G1 " + " ".join([f"{n}{v:.3f}" for n,v in zip("XYZABC", pos)])
                await run_gcode(ws, gcode)
                print(gcode)
                gr = s["gripper"]
                await mqtt.publish("/manipulator/command", gr)
                print(gr)
                await asyncio.sleep(0.5)
            await mqtt.publish("/camera/command", "off")

            end = time.time()
            print(f"Time frame: {begin} {end}")


def main():
    parser = argparse.ArgumentParser(prog="execute")
    parser.add_argument("pathfile")
    parser.add_argument("-o", "--output", default="output")
    parser.add_argument("-m", "--move-to", type=int)
    args = parser.parse_args()

    with open(args.pathfile, "r") as fp:
        path = json.load(fp)

    if isinstance(path, dict):
        # single step
        path = [path]

    assert isinstance(path, list)
    asyncio.run(execute(path, args))


if __name__ == "__main__":
    main()
