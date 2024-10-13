import os
import json
import asyncio

import aiomqtt
from websockets.asyncio.client import connect as ws_connect


MQTT_BROKER_URL = os.environ["MQTT_BROKER_URL"]
MQTT_BROKER_PORT = int(os.environ.get("MQTT_BROKER_PORT", 1883))
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


async def execute(path):
    async with aiomqtt.Client(MQTT_BROKER_URL, port=MQTT_BROKER_PORT) as mqtt:
        async with ws_connect(MOONRAKER_URL) as ws:
            for s in path:
                if pos := s.positions:
                    gcode = "G1 " + " ".join(
                        [f"{n}{v:.3f}" for n,v in zip("XYZABC", pos)]
                    )
                    await run_gcode(ws, gcode)

                if gr := s.gripper:
                    gcode = f"SET_SERVO SERVO=gripper angle={gr}"
                    await run_gcode(ws, gcode)
