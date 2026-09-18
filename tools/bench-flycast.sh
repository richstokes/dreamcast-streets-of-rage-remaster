#!/bin/sh
# Package the action-replay benchmark, run it in background Flycast, and keep
# the serial log as build/logs/<name>-flycast.log. Prints the summary lines.
# Usage: tools/bench-flycast.sh <name> [rom]
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
name=${1:?usage: bench-flycast.sh <name> [rom]}
rom=${2:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
log="$root/build/logs/$name-flycast.log"
SOR_AUDIO=${SOR_AUDIO:-1} SOR_AUDIO_PROFILE=${SOR_AUDIO_PROFILE:-1} \
SOR_REPLAY=${SOR_REPLAY:-"$root/reference/scenarios/phase-aligned-actions.json"} \
    "$root/tools/package.sh" "$rom" > "$root/build/logs/$name-package.log" 2>&1
# Keep the matching ELF so tools/pc-profile.py resolves this run's samples.
cp "$root/build/native/sor.elf" "$root/build/logs/$name.elf"
"$root/tools/run-flycast.sh" "$root/dist/sor.cdi"
# The replay drains its deferred diagnostics once the measured window ends.
deadline=$(( $(date +%s) + ${BENCH_TIMEOUT:-240} ))
until grep -q 'BENCHMARK replay complete' "$root/build/logs/flycast.log" 2>/dev/null; do
    if [ "$(date +%s)" -ge "$deadline" ]; then echo "timeout waiting for $name" >&2; break; fi
    sleep 2
done
sleep 3
cp "$root/build/logs/flycast.log" "$log"
pkill -f "$root/build/flycast-run/sor.cdi" 2>/dev/null || true
grep -E '^(FRAME_STATS|AICA|AUDIO_PARTS)' "$log" | tail -6
