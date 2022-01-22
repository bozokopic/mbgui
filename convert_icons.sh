#!/bin/sh

set -e

ROOT_DIR=$(dirname -- "$0")
ICONS_DIR=$ROOT_DIR/mbgui/icons
FEATHER_DIR=$ROOT_DIR/feather

ICONS_16="inbox folder mail trash flag file eye-off"

mkdir -p $ICONS_DIR

for i in $ICONS_16; do
    convert -background none $FEATHER_DIR/$i.svg \
            -geometry 16x16 $ICONS_DIR/$i-16.png
done
