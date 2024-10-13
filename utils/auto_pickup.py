import argparse
from typing import List

import requests


def predict(predict_url: str, reset: bool = False):
    payload = {
        "image": None,
        "state": None,
        "reset": reset,
    }

    resp = requests.post(predict_url, json=payload)
    resp.raise_for_status()
    return resp.json()


def go(dynamics_url: str, prediction: List[float]):
    payload = {
        "path": [
            {
                "positions": prediction[:6],
                "gripper": prediction[6],
            }
        ]
    }

    resp = requests.post(dynamics_url, json=payload)
    resp.raise_for_status()


def main():
    parser = argparse.ArgumentParser("run autonomously.")
    parser.add_argument("--restart", action="store_true", help="restart the model")
    parser.add_argument("--dynamics-url", default="http://localhost:8003")
    parser.add_argument("--predict-url", default="http://localhost:8000")
    args = parser.parse_args()

    first_step = True

    while True:
        prediction = predict(args.predict_url, reset=first_step and args.restart)
        go(args.dynamics_url, prediction)
        first_step = False
