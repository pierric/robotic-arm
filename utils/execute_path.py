import sys
import os
import json
import asyncio

import aiomqtt
from websockets.asyncio.client import connect as ws_connect
from dotenv import load_dotenv

load_dotenv()

MQTT_SERVER_HOST = os.environ["MQTT_SERVER_HOST"]
MQTT_SERVER_PORT = os.environ.get("MQTT_SERVER_PORT", 1883)
MOONRAKER_URL = os.environ["MOONRAKER_URL"]


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
    #     if "method" not in resp:
    #         print(resp)
    #     elif resp["method"] == "notify_gcode_response":
    #         print(resp)
    #     else:
    #         print(resp["method"])
# {
#     "jsonrpc": "2.0",
#     "method": "notify_gcode_response",
#     "params": ["response message"]
# }


async def execute(path):
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


def main():
    path = json.loads(sys.stdin.read())

    if isinstance(path, dict):
        # single step
        path = [path]

    assert isinstance(path, list)
    asyncio.run(execute(path))


if __name__ == "__main__":
    main()
