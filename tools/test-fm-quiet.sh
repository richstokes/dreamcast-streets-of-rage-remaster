#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
upstream="$root/build/native/upstream"
mkdir -p "$root/build/tests"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined -I"$upstream" \
 "$root/tests/fm_quiet_test.cpp" "$upstream/ymfm_adpcm.cpp" "$upstream/ymfm_ssg.cpp" \
 -o "$root/build/tests/fm-quiet-test"
"$root/build/tests/fm-quiet-test"
