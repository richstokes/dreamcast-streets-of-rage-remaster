#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
upstream="$root/build/native/upstream"
render="$root/src/render/vdp_scene.cpp $root/src/render/sprite_probe.cpp $root/src/render/art_catalog.cpp $root/src/render/scene_light.cpp $root/src/render/scene_particles.cpp $root/src/render/scene_weather.cpp"
vdp="$upstream/VDPRenderer.cpp $upstream/VDPState.cpp $upstream/VDPTile.cpp"
flags="-std=c++23 -O2 -g -fsanitize=address,undefined -I$root/src/dreamcast/compat -I$root/src/render -I$upstream"
clang++ $flags "$root/tests/scene_test.cpp" $render $vdp -lz -o "$root/build/tests/scene-test"
clang++ $flags "$root/tests/enhanced_scene_test.cpp" $render $vdp -lz -o "$root/build/tests/enhanced-scene-test"
"$root/build/tests/scene-test"
"$root/build/tests/enhanced-scene-test"
