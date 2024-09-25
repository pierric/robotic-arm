#!/bin/bash
set -eux
filelist=$(for f in $(basename --suffix .mp4 output/*.mp4); do echo ${f%.mp4}; done)
python to_lerobot_dataset.py --repo-id lerobot/parol6_pickup $filelist
