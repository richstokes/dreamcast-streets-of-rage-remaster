#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
flycast=${FLYCAST_BIN:-"$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast"}
if [ ! -x "$flycast" ]; then flycast=/Applications/Flycast.app/Contents/MacOS/Flycast; fi
exec "$flycast" -config 'config:Debug.SerialConsoleEnabled=yes' "${1:-$root/dist/sor.cdi}"
