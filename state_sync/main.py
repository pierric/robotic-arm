import os
import time
import json
import asyncio

import aiohttp
from dotenv import load_dotenv

load_dotenv()
KLIPPER_SOCKET = os.environ["KLIPPER_SOCKET"]
RESTHEART_URL = os.environ["RESTHEART_URL"]
RESTHEART_TOKEN = os.environ["RESTHEART_TOKEN"]


last_timestamp = 0
FPS = 5
perframe = 1 / FPS
fps = 0
fps_counter_last_timestamp = 0


def gripper_state_to_angle(value: float):
    # as per parol6_octpus.cfg, the maximum_servo_angle is
    # 90, the value that we get from the klipper is half of
    # the pulse width (strangely in unit of 10ms).
    # That is 0° <-> 0.045, and 90° <-> 0.125
    value = max(0.045, min(value, 0.125))
    return (value - 0.045) * 1125


async def query_moonraker(reader, writer, queue):
    global last_timestamp
    global fps
    global fps_counter_last_timestamp

    query_json = json.dumps({
        "id": 100,
        "method": "objects/query",
        "params": {
            "objects": {
                "toolhead": ["homed_axes"],
                "gcode_move": ["gcode_position"],
                "motion_report": ["live_position", "live_velocity"],
                "servo gripper": ["value"],
            }
        }
    }, separators=(",", ":"))

    while(True):
        query_timestamp = time.time()
        writer.write(f"{query_json}\x03".encode())
        resp= await reader.readuntil(b"\x03")
        resp = json.loads(resp[:-1])
        status = resp["result"]["status"]
        homed_axes = status["toolhead"]["homed_axes"]
        position = status["motion_report"]["live_position"]
        velocity = status["motion_report"]["live_velocity"]
        goal = status["gcode_move"]["gcode_position"]

        gripper = status["servo gripper"].get("value")

        homed = homed_axes == "xyzabc"
        if homed:
            payload = {
                "timestamp": query_timestamp,
                "homed": homed,
                "position": position[:6],
                "goal": goal[:6],
                "velocity": velocity,
            }
            if gripper:
                payload["gripper"] = gripper_state_to_angle(gripper)
            await queue.put(payload)

        elasp = time.time() - last_timestamp

        if (elasp < perframe):
            await asyncio.sleep(perframe - elasp)

        last_timestamp = time.time()
        fps += 1

        if last_timestamp - fps_counter_last_timestamp > 5:
            print("fps:", fps / 5)
            fps = 0
            fps_counter_last_timestamp = last_timestamp


async def sync_state(session, queue):
    headers = {
        "Content-Type": "application/json",
        "Authorization": "Basic " + RESTHEART_TOKEN,
    }

    batch = []

    while(True):
        start_time = time.time()
        while len(batch) < 10:
            try:
                batch.append(queue.get_nowait())
            except asyncio.QueueEmpty:
                # wait for more, send anyway if timeout
                if time.time() - start_time > 5:
                    break
                await asyncio.sleep(0)

        if not batch:
            await asyncio.sleep(0)
            continue

        try:
            await session.post(RESTHEART_URL, json=batch, headers=headers, raise_for_status=True)
            batch = []
        except aiohttp.ClientResponseError as e:
            print("error in sync", e)


async def main():
    reader, writer = await asyncio.open_unix_connection(KLIPPER_SOCKET)
    async with aiohttp.ClientSession() as session:
        queue = asyncio.Queue()
        task1 = query_moonraker(reader, writer,queue)
        task2 = sync_state(session, queue)
        await asyncio.gather(task1, task2)


if __name__ == "__main__":
    asyncio.run(main())
