import os
from typing import List, Tuple, Optional

from pydantic import BaseModel
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import roboticstoolbox as rtb
import spatialmath as sm
import numpy as np

import utils

app = FastAPI()

origins = [
    "http://localhost:3000",
    "http://127.0.0.1:3000",
    "http://moon:3000",
    "http://moon.local:3000",
    "*",
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

def from_urdf(file_path, tld=None, xacro_tld=None):
    links, name, urdf_string, urdf_filepath = rtb.Robot.URDF_read(
        file_path, tld=tld, xacro_tld=xacro_tld
    )

    return rtb.Robot(
        links,
        name=name,
        urdf_string=urdf_string,
        urdf_filepath=urdf_filepath,
    )


class ShiftRequest(BaseModel):
    q: List[float]
    offset: Tuple[float, float, float]


class ShiftResponse(BaseModel):
    path: List[List[float]]
    arrived: bool


PACKAGE_DIR = os.path.split(__file__)[0]
# sign because of the direction is different between the real
# hardware and the urdf (TODO fixup the urdf)
REDUCTION_RATIOS = [-6, -20, 20, -4, -4, -10]
# FIXUP on axis-Z because of the difference of point 0
FIXUP = [0, 0, 0, 0, 0, 0]


@app.post("/plan")
def read_item(shift: ShiftRequest):
    parol6 = from_urdf("urdf/PAROL6.urdf.xacro", tld=PACKAGE_DIR, xacro_tld=PACKAGE_DIR)

    path = []
    arrived = False

    q = [v / r + p for v, r, p in zip(shift.q, REDUCTION_RATIOS, FIXUP)]
    Tep = parol6.fkine(q) * sm.SE3.Trans(*shift.offset)

    while not arrived and len(path) < 10:
        v, arrived = rtb.p_servo(parol6.fkine(q), Tep, 1, threshold=0.2)
        qd = np.linalg.pinv(parol6.jacobe(q)) @ v
        norm = np.linalg.norm(qd, 2)
        if norm > 1:
            qd /= norm
        q = q + qd * 0.5
        path.append(q)

    def _revert(q):
        return [(v - p) * r for v, r, p in zip(q, REDUCTION_RATIOS, FIXUP)]

    path = [_revert(q) for q in path]

    return ShiftResponse(path=path, arrived=arrived)


class Step(BaseModel):
    gripper: Optional[float] = None
    positions: Optional[List[float]] = None


class PlanRequest(BaseModel):
    path: List[Step]


@app.post("/execute")
async def execute(plan: PlanRequest):
    await utils.execute(plan.path)
