#!/bin/sh
# Build tools/ym-render.cpp against pinned ymfm and Genesis Plus GX's Nuked OPN2.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
y="$root/research/StreetsOfRageProject/MegaDriveEnvironment"
n="$root/research/Genesis-Plus-GX/core/sound"
mkdir -p "$root/build/tests"
clang -O2 -DHAVE_YM3438_CORE -c "$n/ym3438.c" -I"$n" -o "$root/build/tests/ym3438.o"
clang++ -std=c++20 -O2 -I"$y/include/MegaDriveEnvironment/system/sound/mame_ymfm" -I"$n" "$root/tools/ym-render.cpp" \
  "$y/src/system/sound/mame_ymfm/ymfm_opn.cpp" "$y/src/system/sound/mame_ymfm/ymfm_adpcm.cpp" \
  "$y/src/system/sound/mame_ymfm/ymfm_ssg.cpp" "$root/build/tests/ym3438.o" -o "$root/build/tests/ym-render"
