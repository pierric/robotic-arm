#!/bin/bash
if [ $# -lt 1 ]; then
    echo "error: $0 <repo-id>"
    exit 1
fi

REPO_ID=$1

set -eux
filelist=$(for f in $(basename --suffix .mp4 output/*.mp4); do echo ${f%.mp4}; done)
python to_lerobot_dataset.py --repo-id $REPO_ID $filelist
