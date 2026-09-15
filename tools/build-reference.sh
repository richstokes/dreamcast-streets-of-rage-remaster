#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/local/SOR.bin"}}
python3 "$root/tools/rom.py" "$rom" --output "$root/build/reference-rom.json"
python3 "$root/tools/bootstrap.py"
"$root/research/StreetsOfRageProject/scripts/generate_cpp" "$rom"
cmake -S "$root/research/StreetsOfRageProject/StreetsOfRageRecompilation" \
    -B "$root/build/reference" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/reference" --parallel "${JOBS:-4}"
printf 'Reference executable: %s/build/reference/sor\n' "$root"
