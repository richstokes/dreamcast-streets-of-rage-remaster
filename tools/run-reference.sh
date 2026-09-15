#!/bin/sh
# The comparison server is headless by default, including on macOS.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/local/SOR.bin"}}
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
exec "$root/build/reference-patched/sor" --rom "$rom" --lang en --hz 60 \
    --silent --debugUtils --port "${SOR_REMOTE_PORT:-7777}" \
    --auxAddrFile "$root/build/reference-missing.txt"
