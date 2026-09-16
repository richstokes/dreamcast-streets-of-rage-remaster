#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}
mkdir -p "$root/build/tests"
python3 "$root/tools/extract-dac-reference.py" "$rom" "$root/build/tests/dac-reference.bin"
upstream="$root/build/native/upstream"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined -I"$root/src/audio" -I"$upstream" \
 "$root/tests/dac_integration_test.cpp" "$root/src/audio/audio_core.cpp" "$root/src/audio/dac_driver.cpp" \
 "$upstream/ymfm_opn.cpp" "$upstream/ymfm_adpcm.cpp" "$upstream/ymfm_ssg.cpp" \
 -o "$root/build/tests/dac-integration-test"
"$root/build/tests/dac-integration-test" "$rom" "$root/build/tests/dac-reference.bin"
