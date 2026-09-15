#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
flycast=${FLYCAST_BIN:-"$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast"}
if [ ! -x "$flycast" ]; then flycast=/Applications/Flycast.app/Contents/MacOS/Flycast; fi
mkdir -p "$root/build/logs"
image=${1:-$root/dist/sor.cdi}
case "$image" in /*) ;; *) image="$PWD/$image" ;; esac
if [ "$(uname -s)" = Darwin ]; then
    # Best effort only: Flycast may override these flags and show its window.
    app=${flycast%/Contents/MacOS/*}
    if [ "$app" = "$flycast" ]; then
        echo 'On macOS FLYCAST_BIN must point inside a Flycast.app bundle.' >&2
        exit 1
    fi
    exec /usr/bin/open -g -j -n -a "$app" \
        --stdout "$root/build/logs/flycast.log" --stderr "$root/build/logs/flycast-errors.log" \
        --args -config 'config:Debug.SerialConsoleEnabled=yes' "$image"
fi
echo 'Automatic launch is configured only for background macOS app bundles.' >&2
exit 1
