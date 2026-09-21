#!/bin/sh
# Build the placeholder art set (build/art/SORART.PAK) with every player
# character: for Adam, Axel (RIGHT on the select screen) and Blaze (LEFT) it
# replays the scripted Round 1, action and combat scenarios and two bot runs
# (plain; throws and pickups), extracts the object frames each run draws, and
# packs them together with the two-player bot replay's.
# Needs the headless build and the profiling core (tools/build-profile-core.sh).
# Usage: tools/make-art-set.sh [rom]
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
py="$root/build/tools-venv/bin/python3"; core="$root/build/gpgx-profile/genesis_plus_gx_libretro.dylib"
work="$root/build/art-set"; frames="$root/build/frames-set"
rm -rf "$work" "$frames"; mkdir -p "$work" "$frames"
python3 - "$root" "$work" <<'PY'
import json,sys
root,work=sys.argv[1:]
def variant(src,key):
    d=json.load(open(f'{root}/reference/scenarios/{src}.json'))['segments']
    assert d[6].get('capture')=='select' and d[7]=={'frames':1,'p1':['START']},src
    return d[:7]+([] if key is None else [{'frames':2,'p1':[key]},{'frames':30}])+d[7:]
for name,key in (('adam',None),('axel','RIGHT'),('blaze','LEFT')):
    for src in ('round1-full','phase-aligned-actions','round1-combat'):
        json.dump({'segments':variant(src,key)},open(f'{work}/{name}-{src}.json','w'))
    # Up to and including the phase anchor: where the bot takes over.
    json.dump({'segments':variant('round1-full',key)[:10 if key is None else 12]},open(f'{work}/{name}-prologue.json','w'))
json.dump(json.load(open(f'{root}/reference/scenarios/two-player-bot.json')),open(f'{work}/two-player-bot.json','w'))
PY
for c in adam axel blaze; do
    "$py" "$root/tools/bot-play.py" "$core" "$rom" "$work/$c-prologue.json" 16000 "$work/$c-bot.json" --weaken > /dev/null &
    "$py" "$root/tools/bot-play.py" "$core" "$rom" "$work/$c-prologue.json" 16000 "$work/$c-bot-items.json" --weaken --throws --pickups > /dev/null &
done; wait
for scenario in "$work"/*.json; do
    name=$(basename "$scenario" .json); case $name in *-prologue) continue;; esac
    ( python3 "$root/tools/replay.py" "$scenario" "$work/$name.bin" > /dev/null; mkdir -p "$frames/$name"
      SOR_EXTRACT_FRAMES="$frames/$name" "$root/build/headless/sor-headless" "$rom" "$work/$name.bin" /dev/null > /dev/null 2>&1 ) &
done; wait
"$py" "$root/tools/make-placeholder-art.py" "$frames"/* --out "$root/build/art/SORART.PAK"
