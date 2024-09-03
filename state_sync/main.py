import os
import time
import asyncio

import aiohttp
from dotenv import load_dotenv

load_dotenv()
MOONRAKER_URL = os.environ["MOONRAKER_URL"]
RESTHEART_URL = os.environ["RESTHEART_URL"]
RESTHEART_TOKEN = os.environ["RESTHEART_TOKEN"]


last_timestamp = 0
FPS = 30
perframe = 1 / FPS
fps = 0
fps_counter_last_timestamp = 0


async def query_moonraker(session, queue):
    global last_timestamp
    global fps
    global fps_counter_last_timestamp

    while(True):
        try:
            res = await session.get(MOONRAKER_URL, raise_for_status=True)
        except aiohttp.ClientResponseError as e:
            print("error in query", e)
        else:
            res = await res.json()
            status = res["result"]["status"]
            homed_axes = status["toolhead"]["homed_axes"]
            positions = status["gcode_move"]["gcode_position"]
            await queue.put({"homed": homed_axes == "xyzabc", "positions": positions})

        elasp = time.time() - last_timestamp

        if (elasp < perframe):
            await asyncio.sleep(perframe - elasp)

        last_timestamp = time.time()
        fps += 1

        if last_timestamp - fps_counter_last_timestamp > 1:
            print("fps:", fps)
            fps = 0
            fps_counter_last_timestamp = last_timestamp


async def sync_state(session, queue):
    headers = {
        "Content-Type": "application/json",
        "Authorization": "Basic " + RESTHEART_TOKEN,
    }

    while(True):
        doc = await queue.get()
        payload = {
            "timestamp": time.time(),
            **doc
        }
        try:
            await session.post(RESTHEART_URL, json=payload, headers=headers, raise_for_status=True)
        except aiohttp.ClientResponseError as e:
            print("error in sync", e)


async def main():
    async with aiohttp.ClientSession() as session:
        queue = asyncio.Queue()
        task1 = query_moonraker(session, queue)
        task2 = sync_state(session, queue)
        await asyncio.gather(task1, task2)


if __name__ == "__main__":
    asyncio.run(main())
