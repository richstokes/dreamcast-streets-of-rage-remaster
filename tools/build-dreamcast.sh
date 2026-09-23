#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
"$root/tools/check-requirements.sh" python kos research
# KOS's environment expects some unset shell variables.
set +u
source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"
set -u
python3 "$root/tools/prepare-native.py" --dreamcast
"${PYTHON:-python3.14}" "$root/tools/arithmetic-probes.py"
make -C "$root/src/dreamcast" -j"${JOBS:-4}" all
sh-elf-readelf -h "$root/build/native/sor.elf"
sh-elf-size "$root/build/native/sor.elf"
