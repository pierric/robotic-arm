import os
import io
import asyncio
import base64
from datetime import datetime
import time

import aiomqtt
import aiohttp
from PIL import Image, ExifTags

PART_BOUNDARY = b"123456789000000000000987654321"
_STREAM_BOUNDARY = (b"\r\n--" + PART_BOUNDARY + b"\r\n")
TIME_FMT = "%Y:%m:%d %H:%M:%S.%fZ%z"

CAM_STREAMING_URL = os.environ["CAM_STREAMING_URL"]
RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
MQTT_BROKER_URL = os.environ["MQTT_BROKER_URL"]

RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"]
}

camera_enabled = False

async def process(session: aiohttp.ClientSession, streaming: aiohttp.ClientResponse):

    full_chunk = b""

    async for chunk, end in streaming.content.iter_chunks():
        if not camera_enabled:
            await session.close()
            break

        if chunk == _STREAM_BOUNDARY:
            #print("boundary")
            pass

        elif chunk.startswith(b"Content-Type: image/jpeg"):
            #print("start")
            pass

        else:
            full_chunk += chunk

            if not end:
                continue

            image = Image.open(io.BytesIO(full_chunk))
            exif = image.getexif()
            idf = exif.get_ifd(ExifTags.IFD.Exif)
            time = exif[0x132]
            usec = idf[0x9290]
            gripper_state = idf[0xA436]
            timestamp = datetime.strptime(f"{time}.{usec}Z+0000", TIME_FMT).timestamp()
            enc = base64.b64encode(full_chunk)

            payload = {
                "timestamp": timestamp,
                "image": enc.decode(),
            }
            async with session.post(
                f"{RESTHEART_ENDPOINT}/camera",
                json=payload,
                headers=RESTHEART_HEADERS) as res:

                if res.status not in (200, 201):
                    print(f"failed to send image to restheart [{res.status_code}]")

            payload = {
                "timestamp": timestamp,
                "state": float(gripper_state),
            }
            async with session.post(
                f"{RESTHEART_ENDPOINT}/gripper",
                json=payload,
                headers=RESTHEART_HEADERS) as res:

                if res.status not in (200, 201):
                    print(f"failed to send gripper state to restheart [{res.status_code}]")

            full_chunk = b''


async def mqtt_listen():

    global camera_enabled

    async with aiomqtt.Client(MQTT_BROKER_URL) as client:
        await client.subscribe("/camera/command")
        print("mqtt subscribed.")
        async for message in client.messages:
            cmd = message.payload.decode().lower()
            print("mqtt message:", cmd)

            if cmd not in ["on", "off"]:
                print("Warning: unknown camera command", cmd)

            camera_enabled = cmd == "on"


async def stream_receive():
    while True:
        if not camera_enabled:
            await asyncio.sleep(1)
            continue

        async with aiohttp.ClientSession() as session:
            print("Connecting to camera..")

            try:
                res = await session.get(CAM_STREAMING_URL)
            except aiohttp.ClientConnectionError:
                print("Error: camera not online, waiting..")
                await asyncio.sleep(5)
                continue

            print("Connected")
            await process(session, res)


def main():
    async def _ep():
        await asyncio.gather(mqtt_listen(), stream_receive())

    asyncio.run(_ep())


if __name__ == "__main__":
    main()
