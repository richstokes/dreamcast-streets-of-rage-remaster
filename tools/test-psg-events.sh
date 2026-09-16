#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
upstream="$root/build/native/upstream"
mkdir -p "$root/build/tests"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined \
 -I"$root/src/audio" -I"$upstream" \
 "$root/tests/psg_events_test.cpp" "$root/src/audio/audio_core.cpp" "$root/src/audio/dac_driver.cpp" \
 "$upstream/ymfm_opn.cpp" "$upstream/ymfm_adpcm.cpp" "$upstream/ymfm_ssg.cpp" \
 -o "$root/build/tests/psg-events-test"
"$root/build/tests/psg-events-test"
