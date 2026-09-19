#!/bin/sh
# Build the host-only MC68000 timing tool (Musashi via Genesis Plus GX research checkout).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
g="$root/research/Genesis-Plus-GX/core"
mkdir -p "$root/build/tests"
clang -O2 -DLSB_FIRST -c "$g/m68k/m68kcpu.c" -I"$g" -I"$g/m68k" -o "$root/build/tests/m68kcpu.o"
clang++ -std=c++20 -O2 -DLSB_FIRST -I"$g" -I"$g/m68k" "$root/tools/m68k-time.cpp" "$root/build/tests/m68kcpu.o" -o "$root/build/tests/m68k-time"
