import os
import io
import json
import asyncio
import base64
from datetime import datetime, timezone

import aiohttp
import aiomqtt
from PIL import Image, UnidentifiedImageError

PART_BOUNDARY = b"123456789000000000000987654321"
_STREAM_BOUNDARY = (b"\r\n--" + PART_BOUNDARY + b"\r\n")
TIME_FMT = "%Y:%m:%d %H:%M:%S.%fZ%z"

CAM_STREAMING_URL = os.environ["CAM_STREAMING_URL"]
RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]
MOONRAKER_URL = os.environ["MOONRAKER_URL"]
MQTT_BROKER_URL = os.environ["MQTT_BROKER_URL"]

RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"]
}

TagTiffDateTime = 0x132
TagTiffExifIFD = 0x8769
TagExifSubSecTime = 0x9290

stopFlag = False
recordFlag = True

async def _process_chunk(session, chunk):

    try:
        cs = chunk.split(b"\r\n", 3)
        if len(cs) != 4 or (
            not cs[0].startswith(b"Content-Type:") or
            not cs[1].startswith(b"Content-Length:") or
            cs[2] != b""
        ):
            print("Abnormal chunk", chunk[:40])
        chunk = cs[3]
        image = Image.open(io.BytesIO(chunk))
        exif = image.getexif()
        now = exif.get(TagTiffDateTime)
        millisec = exif.get_ifd(TagTiffExifIFD).get(TagExifSubSecTime)
    except UnidentifiedImageError:
        print("bad or incomplete chunk, image cannot be decoded")
        return

    enc = base64.b64encode(chunk)

    timestamp = datetime.fromisoformat(now).replace(
        tzinfo=timezone.utc,
        microsecond=int(millisec) * 1000
    ).timestamp()

    payload = {
        "timestamp": timestamp,
        "image": enc.decode(),
    }
    async with session.post(
        f"{RESTHEART_ENDPOINT}/camera",
        json=payload,
        headers=RESTHEART_HEADERS) as res:

        if res.status not in (200, 201):
            print(f"failed to send image to restheart [{res.status}]")


async def process(session: aiohttp.ClientSession, streaming: aiohttp.ClientResponse):

    full_chunk = b""

    async for chunk, end in streaming.content.iter_chunks():

        if stopFlag or not recordFlag:
            print("Recording stopped.")
            return

        # assuming that the boudnary is never received in pieces
        if (bpos := chunk.find(_STREAM_BOUNDARY)) < 0:
            full_chunk += chunk
        else:
            full_chunk += chunk[:bpos]
            if full_chunk != b"":
                await _process_chunk(session, full_chunk)
            full_chunk = chunk[bpos + len(_STREAM_BOUNDARY):]


async def stream_receive(session):
    while True:
        # print(f"streaming {stopFlag} {recordFlag}")
        if stopFlag or not recordFlag:
            await asyncio.sleep(0.5)
            continue

        print("Connecting to camera: ", CAM_STREAMING_URL)

        try:
            res = await session.get(CAM_STREAMING_URL)
        except (aiohttp.ClientConnectionError, asyncio.TimeoutError):
            print("Camera is not online, waiting..")
            continue

        print("Connected")
        try:
            await process(session, res)
        except asyncio.TimeoutError:
            continue


async def check_camera_status(session):
    global stopFlag
    global recordFlag
    while True:
        try:
            resp = await session.get(
                MOONRAKER_URL + "/printer/objects/query?output_pin camera_en",
            )
        except (aiohttp.ClientConnectionError, asyncio.TimeoutError):
            stopFlag = True
            recordFlag = False
            await asyncio.sleep(0.5)
            continue

        if resp.status != 200:
            await asyncio.sleep(0.5)
            continue

        status = await resp.json()
        stopFlag = status["result"]["status"]["output_pin camera_en"]["value"] != 1
        await asyncio.sleep(2)


async def check_mqtt_command():
    global recordFlag
    async with aiomqtt.Client(MQTT_BROKER_URL) as client:
        await client.subscribe("/camera/record")
        print("mqtt subscribed.")
        async for message in client.messages:
            cmd = message.payload.decode().lower()
            print("mqtt message:", cmd)
            if cmd not in ["on", "off"]:
                print("Warning: unknown camera command", cmd)

            recordFlag = cmd == "on"
            await asyncio.sleep(0.5)


def main():
    timeout = aiohttp.ClientTimeout(
        sock_connect=2,
        sock_read=2,
    )

    async def _ep():
        async with aiohttp.ClientSession(timeout=timeout) as session:
            await asyncio.gather(
                check_camera_status(session),
                check_mqtt_command(),
                stream_receive(session),
            )
    asyncio.run(_ep())


if __name__ == "__main__":
    main()
