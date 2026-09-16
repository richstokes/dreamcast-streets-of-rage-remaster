#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined \
 -I"$root/src/audio" -I"$root/build/native/upstream" \
 "$root/tests/z80_hot_test.cpp" -o "$root/build/tests/z80-hot-test"
"$root/build/tests/z80-hot-test"
