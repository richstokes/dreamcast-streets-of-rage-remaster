#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
upstream="$root/build/native/upstream"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined \
 -I"$root/src/dreamcast/compat" -I"$root/src/render" -I"$upstream" \
 "$root/tests/scene_test.cpp" "$root/src/render/vdp_scene.cpp" \
 "$upstream/VDPRenderer.cpp" "$upstream/VDPState.cpp" "$upstream/VDPTile.cpp" \
 -o "$root/build/tests/scene-test"
"$root/build/tests/scene-test"
