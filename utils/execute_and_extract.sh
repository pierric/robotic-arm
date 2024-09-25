#!/bin/bash

if [ $# -lt 1 ]; then
    echo "error: $0 <path-name>"
    exit 1
fi

set -eux
PATHNAME=$1

TIMESPAN=$(python execute_path.py paths/$PATHNAME.json | tail -n 1 | cut -d : -f 2)
echo $TIMESPAN
python extract_dataset.py --from-path paths/$PATHNAME.json $TIMESPAN

