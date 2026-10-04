#!/bin/bash
# One command builds a self-contained development ELF and launches Flycast.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
"$root/tools/check-requirements.sh" flycast
"$root/tools/build-test-elf.sh" "$@"
echo 'Launching dist/sor-test.elf (contains your ROM; keep local).'
exec "$root/tools/run-flycast.sh" "$root/dist/sor-test.elf"
