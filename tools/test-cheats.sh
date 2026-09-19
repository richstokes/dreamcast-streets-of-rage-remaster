#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang -std=c11 -O2 -g -fsanitize=address,undefined -I"$root/include" \
    -c "$root/src/core/memory.c" -o "$root/build/tests/cheats-memory.o"
clang++ -std=c++23 -Wall -Wextra -Werror -O2 -g -fsanitize=address,undefined \
    -I"$root/include" -I"$root/src/dreamcast" -I"$root/src/dreamcast/compat" -I"$root/build/native/upstream" \
    "$root/tests/cheats_test.cpp" "$root/src/dreamcast/cheats.cpp" "$root/build/tests/cheats-memory.o" \
    -o "$root/build/tests/cheats-test"
"$root/build/tests/cheats-test"
