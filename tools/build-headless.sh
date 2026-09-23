#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$root/tools/check-requirements.sh" python host research
python3 "$root/tools/prepare-native.py"
cmake -S "$root/src/headless" -B "$root/build/headless" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/headless" --parallel "${JOBS:-4}"
