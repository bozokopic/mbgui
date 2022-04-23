#!/bin/sh

set -e

cd $(dirname -- "$0")

PYTHON=${PYTHON:-python3}

rm -rf ./build
$PYTHON setup.py \
    build --build-base build \
    egg_info --egg-base build \
    bdist_wheel --dist-dir build
