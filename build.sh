#!/bin/sh

set -e

cd $(dirname -- "$0")

LIBS="gtk+-3.0 gio-2.0 gio-unix-2.0 glib-2.0"
CC=${CC:-cc}

mkdir -p build
$CC -o build/mbgui src_c/*.c $(pkg-config --cflags --libs $LIBS)
