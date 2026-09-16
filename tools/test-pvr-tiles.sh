#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined -I"$root/src/render" "$root/tests/pvr_tiles_test.cpp" -o "$root/build/tests/pvr-tiles"
"$root/build/tests/pvr-tiles"
