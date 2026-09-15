#!/bin/bash
set -eo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"
python3 "$root/tools/prepare-native.py"
make -C "$root/src/dreamcast" -j"${JOBS:-4}" all
sh-elf-readelf -h "$root/build/native/sor.elf"
sh-elf-size "$root/build/native/sor.elf"
