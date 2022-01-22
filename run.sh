#!/bin/sh

set -e

PYTHON=${PYTHON:-python3}
ROOT_DIR=$(dirname -- "$0")

export PYTHONPATH=$ROOT_DIR

exec $PYTHON -m mbgui "$@"
