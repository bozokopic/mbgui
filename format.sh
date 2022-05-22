#!/bin/sh

set -e

cd $(dirname -- "$0")

clang-format -style=file -i src_c/*.c src_c/*.h
