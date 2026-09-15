#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/local/SOR.bin"}}
python3 "$root/tools/rom.py" "$rom" --require-known --output "$root/build/disc-rom.json"
"$root/tools/build-dreamcast.sh"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1789498800}
mkdcdisc=${MKDCDISC:-"$root/build/mkdcdisc/build/mkdcdisc"}
mkdir -p "$root/build/disc" "$root/dist"
cp "$rom" "$root/build/disc/SOR.BIN"
if [ -n "${SOR_REPLAY:-}" ]; then
 python3 "$root/tools/replay.py" "$SOR_REPLAY" "$root/build/disc/REPLAY.BIN"
else
 rm -f "$root/build/disc/REPLAY.BIN"
fi
"$mkdcdisc" --allow-overwrite --main-elf "$root/build/native/sor.elf" \
 --directory-contents "$root/build/disc" --title 'SOR NATIVE CHECKPOINT' \
 --author richstokes --output "$root/dist/sor.cdi"
cp "$root/build/native/sor.elf" "$root/dist/sor.elf"
shasum -a 256 "$root/dist/sor.elf" "$root/dist/sor.cdi" > "$root/dist/SHA256SUMS"
echo 'Local artifacts contain your game data. Do not commit or redistribute them.'
