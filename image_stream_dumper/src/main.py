import os
import io
import base64
from datetime import datetime
import time

import requests
from PIL import Image, ExifTags

PART_BOUNDARY = b"123456789000000000000987654321"
_STREAM_BOUNDARY = (b"\r\n--" + PART_BOUNDARY + b"\r\n")
TIME_FMT = "%Y:%m:%d %H:%M:%S.%fZ%z"

CAM_STREAMING_URL = os.environ["CAM_STREAMING_URL"]
RESTHEART_ENDPOINT = os.environ["RESTHEART_ENDPOINT"]

RESTHEART_HEADERS = {
    "Content-Type": "application/json",
    "Authorization": "Basic " + os.environ["RESTHEART_TOKEN"]
}

def process(response: requests.Response):
    for chunk in res.iter_content(chunk_size=None):
        if chunk == _STREAM_BOUNDARY:
            # print("boundary")
            pass

        elif chunk.startswith(b"Content-Type: image/jpeg"):
            # print("start")
            pass

        else:
            image = Image.open(io.BytesIO(chunk))
            exif = image.getexif()
            idf = exif.get_ifd(ExifTags.IFD.Exif)
            time = exif[0x132]
            usec = idf[0x9290]
            gripper_state = idf[0xA436]
            timestamp = datetime.strptime(f"{time}.{usec}Z+0000", TIME_FMT).timestamp()
            enc = base64.b64encode(chunk)
            payload = {
                "timestamp": timestamp,
                "image": enc.decode(),
            }
            res = requests.post(
                f"{RESTHEART_ENDPOINT}/camera",
                json=payload,
                headers=RESTHEART_HEADERS
            )
            if res.status_code not in (200, 201):
                print(f"failed to send image to restheart [{res.status_code}]")

            payload = {
                "timestamp": timestamp,
                "state": float(gripper_state),
            }
            res = requests.post(
                f"{RESTHEART_ENDPOINT}/gripper",
                json=payload,
                headers=RESTHEART_HEADERS
            )
            if res.status_code not in (200, 201):
                print(f"failed to send gripper state to restheart [{res.status_code}]")

def main():
    while(True):
        print("connecting to camera..")
        try:
            res = requests.get(CAM_STREAMING_URL, stream=True)
        except requests.exceptions.ConnectionError:
            print("camera not online, waiting..")
            time.sleep(5)
            continue
        process(res)

if __name__ == "__main__":
    main()
