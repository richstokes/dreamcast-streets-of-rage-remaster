#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}
mkdir -p "$root/build/tests"
python3 "$root/tools/extract-dac-reference.py" "$rom" "$root/build/tests/dac-reference.bin"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined -I"$root/src/audio" -I"$root/build/native/upstream" \
 "$root/tests/dac_driver_test.cpp" "$root/src/audio/dac_driver.cpp" -o "$root/build/tests/dac-driver-test"
"$root/build/tests/dac-driver-test" "$rom" "$root/build/tests/dac-reference.bin"
