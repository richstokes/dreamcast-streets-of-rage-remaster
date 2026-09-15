#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
python3 tools/prepare-native.py
mkdir -p build/tests
cp research/StreetsOfRageProject/MegaDriveEnvironment/include/MegaDriveEnvironment/system/graphics/VDPRenderer.hpp build/tests/reference-renderer.hpp
c++ -std=c++23 -O2 -fsanitize=address,undefined -DVDPRenderer=ReferenceRenderer \
 -Isrc/dreamcast/compat -Ibuild/native/upstream \
 -c research/StreetsOfRageProject/MegaDriveEnvironment/src/system/graphics/VDPRenderer.cpp -o build/tests/reference-renderer.o
c++ -std=c++23 -O2 -fsanitize=address,undefined -Isrc/dreamcast/compat -Ibuild/native/upstream -Ibuild/tests \
 tests/renderer_test.cpp build/tests/reference-renderer.o build/native/upstream/VDPState.cpp \
 build/native/upstream/VDPRenderer.cpp build/native/upstream/VDPTile.cpp -o build/tests/renderer_test
build/tests/renderer_test
