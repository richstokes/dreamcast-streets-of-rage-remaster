#!/bin/bash
# Regenerate the latest game code and build a self-booting CDI image.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
rom=${1:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
"$root/tools/check-requirements.sh" "rom=$rom" python git kos mkdcdisc
if [ ! -d "$root/research/StreetsOfRageProject/RageDecompiler/tools" ]; then
    python3 "$root/tools/bootstrap.py"
fi
export SOR_ROM="$rom"
"${PYTHON:-python3.14}" "$root/tools/generate.py" "$rom"
"$root/tools/package.sh" "$rom"
echo "Built $root/dist/sor.cdi"
