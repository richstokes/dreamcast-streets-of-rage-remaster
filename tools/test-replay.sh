#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined \
 -I"$root/src/dreamcast" -I"$root/src/dreamcast/compat" \
 "$root/tests/replay_test.cpp" "$root/src/dreamcast/replay.cpp" -o "$root/build/tests/replay-test"
"$root/build/tests/replay-test"
