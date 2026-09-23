#!/bin/bash
# One command builds a self-contained development ELF and launches Flycast.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
rom=${1:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
"$root/tools/check-requirements.sh" "rom=$rom" python git kos bin2o flycast
if [ ! -d "$root/research/StreetsOfRageProject/RageDecompiler/tools" ]; then
    python3 "$root/tools/bootstrap.py"
fi
"${PYTHON:-python3.14}" "$root/tools/generate.py" "$rom"
# This development build starts in enhanced graphics; the options menu (L + R)
# switches back to the original at any time.
export SOR_ENHANCED=${SOR_ENHANCED:-1}
"$root/tools/build-dreamcast.sh"
# KOS's environment expects some unset shell variables.
set +u
source "${KOS_ENV:-$HOME/.local/share/dreamcast/kos/environ.sh}"
set -u
"$KOS_BASE/utils/bin2o/bin2o" "$rom" sor_embedded_rom "$root/build/native/embedded-rom.o"
# Replacement art for enhanced graphics, when a package has been built
# (tools/make-art-set.sh). There is no disc in this flow, so it is
# embedded in the executable.
art=${SOR_ART:-"$root/build/art/SORART.PAK"}
if [ -f "$art" ]; then
    "$KOS_BASE/utils/bin2o/bin2o" "$art" sor_embedded_art "$root/build/native/embedded-art.o"
else
    rm -f "$root/build/native/embedded-art.o"
fi
make -C "$root/src/dreamcast" -j"${JOBS:-4}" ../../build/native/sor-test.elf
mkdir -p "$root/dist"
cp "$root/build/native/sor-test.elf" "$root/dist/sor-test.debug.elf"
# Flycast's ELF loader rejects files over 16 MiB, including debug sections.
# Keep full symbols separately and launch an ELF containing the same load segments.
"$KOS_OBJCOPY" --strip-debug "$root/build/native/sor-test.elf" "$root/dist/sor-test.elf"
echo 'Launching dist/sor-test.elf (contains your ROM; keep local).'
exec "$root/tools/run-flycast.sh" "$root/dist/sor-test.elf"
