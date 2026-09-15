#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/local/SOR.bin"}}
python3 "$root/tools/rom.py" "$rom" --output "$root/build/reference-rom.json"
python3 "$root/tools/bootstrap.py"
"${PYTHON:-python3.14}" "$root/tools/generate.py" "$rom"
cmake -S "$root/research/StreetsOfRageProject/StreetsOfRageRecompilation" \
    -B "$root/build/reference" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/reference" --parallel "${JOBS:-4}"
python3 "$root/tools/prepare-reference.py"
cmake -S "$root/research/StreetsOfRageProject/StreetsOfRageRecompilation" \
    -B "$root/build/reference-patched" -DCMAKE_BUILD_TYPE=Release \
    -DMEGADRIVE_ENVIRONMENT_DIR="$root/build/reference-runtime-v2"
cmake --build "$root/build/reference-patched" --parallel "${JOBS:-4}"
printf 'Reference executable: %s/build/reference/sor\n' "$root"
printf 'Headless lockstep executable: %s/build/reference-patched/sor\n' "$root"
