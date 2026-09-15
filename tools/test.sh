#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I"$root/include" \
 "$root/tests/core_test.c" "$root/src/core/memory.c" "$root/src/core/save.c" -o "$root/build/tests/core_test"
"$root/build/tests/core_test"
