#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang -std=c11 -O2 -g -fsanitize=address,undefined -I"$root/include" -c "$root/src/core/memory.c" -o "$root/build/tests/native-memory-core.o"
clang++ -std=c++23 -O2 -g -fsanitize=address,undefined -I"$root/include" -I"$root/src/dreamcast/compat" -I"$root/build/native/upstream" \
 "$root/tests/native_memory_test.cpp" "$root/build/tests/native-memory-core.o" -o "$root/build/tests/native-memory-test"
"$root/build/tests/native-memory-test"
