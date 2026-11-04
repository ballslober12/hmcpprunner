#!/bin/sh
# Hotline Miami for LG NetCast 4.x, started by the TV app manager (e.g. webos-userland's
# "nc4app run hmcpprunner/hmcpprunner.sh"). The --service/--auid arguments must reach the game:
# SDL's nc4 video driver registers with them.
DIR=$(cd "$(dirname "$0")" && pwd -P)
cd "$DIR" || exit 1
# HM_PAD_LOG=1 writes the raw gamepad events to work/pad.log (to add support for an unknown pad)
exec ./hmcpprunner --assets "$DIR/assets" --work "$DIR/work" --data "$DIR/data/game.dat" "$@" >> "$DIR/hmcpprunner.log" 2>&1
