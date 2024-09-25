import os
from typing import List
from pathlib import Path
import base64

from lerobot.common.policies.act.modeling_act import ACTPolicy
from pydantic import BaseModel
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import torch
import torchvision


neptune_id = os.environ["NEPTUNE_ID"]

pretrained_policy_path = Path(f"outputs/train/parol6_pickup/{neptune_id}")
policy = ACTPolicy.from_pretrained(pretrained_policy_path)
policy.eval()
policy.to("cuda")


app = FastAPI()
origins = [
    "http://localhost:3000",
    "*",
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

class PredictRequest(BaseModel):
    image: str
    state: List[float]
    reset: bool = False


@app.post("/predict")
async def predict(req: PredictRequest) -> List[float]:
    bs = base64.b64decode(req.image)
    buf = torch.frombuffer(bs, dtype=torch.uint8)
    img = torchvision.io.decode_jpeg(buf)

    img = img.to(torch.float32) / 255
    img = img.to("cuda", non_blocking=True)
    img = img.unsqueeze(0)

    sta = torch.as_tensor(req.state, dtype=torch.float32)
    sta = sta.to("cuda", non_blocking=True)
    sta = sta.unsqueeze(0)

    if req.reset:
        policy.reset()

    observation = {
        "observation.images.top": img,
        "observation.state": sta,
    }

    with torch.inference_mode():
        action = policy.select_action(observation)

    action = action.squeeze(0).to("cpu").numpy()
    return list(action)
