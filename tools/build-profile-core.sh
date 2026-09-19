#!/bin/sh
# Build a Genesis Plus GX core with a per-PC 68000 cycle histogram (analysis only;
# the reference core in research/ is left untouched). Output:
# build/gpgx-profile/genesis_plus_gx_libretro.dylib, used with genesis_reference.py --profile.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rm -rf "$root/build/gpgx-profile"
cp -R "$root/research/Genesis-Plus-GX" "$root/build/gpgx-profile"
cat "$root/tools/gpgx-profile-hook.c" >> "$root/build/gpgx-profile/core/debug/cpuhook.c"
cd "$root/build/gpgx-profile"
make -f Makefile.libretro clean > /dev/null
make -f Makefile.libretro HOOK_CPU=1 -j"${JOBS:-8}"
