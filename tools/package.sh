#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
"$root/tools/check-requirements.sh" "rom=$rom" mkdcdisc
python3 "$root/tools/rom.py" "$rom" --require-known --output "$root/build/disc-rom.json"
"$root/tools/build-dreamcast.sh"
# Fixed timestamp (2026-09-15) so the same inputs give the same image.
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1789498800}
mkdcdisc=${MKDCDISC:-"$root/build/mkdcdisc/build/mkdcdisc"}
[ -x "$mkdcdisc" ] || mkdcdisc=$(command -v mkdcdisc)
mkdir -p "$root/build/disc" "$root/dist"
cp "$rom" "$root/build/disc/SOR.BIN"
# Replacement art for enhanced graphics, built from the ROM by
# tools/make-art-set.sh; a local build product.
art=${SOR_ART:-"$root/build/art/SORART.PAK"}
if [ -f "$art" ]; then cp "$art" "$root/build/disc/SORART.PAK"; else rm -f "$root/build/disc/SORART.PAK"; fi
if [ -n "${SOR_REPLAY:-}" ]; then
 python3 "$root/tools/replay.py" "$SOR_REPLAY" "$root/build/disc/REPLAY.BIN"
else
 rm -f "$root/build/disc/REPLAY.BIN"
fi
"$mkdcdisc" --allow-overwrite --main-elf "$root/build/native/sor.elf" \
 --directory-contents "$root/build/disc" --title 'STREETS OF RAGE' \
 --author "${SOR_AUTHOR:-SOR REMASTER}" --output "$root/dist/sor.cdi"
cp "$root/build/native/sor.elf" "$root/dist/sor.elf"
shasum -a 256 "$root/dist/sor.elf" "$root/dist/sor.cdi" > "$root/dist/SHA256SUMS"
echo 'Local artifacts contain your game data. Do not commit or redistribute them.'
