#!/bin/sh
# Build the replacement art set (build/art/SORART.PAK). See docs/REMASTER.md.
#
#  1. Player runs: as Adam, Axel (RIGHT on the select screen) and Blaze (LEFT),
#     the scripted Round 1, action and combat scenarios and two bot runs. They
#     supply the players' colours and check the ROM extraction.
#  2. Round sweeps: a scripted run from each round (SOR_CHEATS: round select,
#     infinite health, lives and specials; walk, punch, occasional police
#     special; Round 8 runs right to left). Whenever an enemy, boss, item or
#     effect is drawn, the extractor renders its whole animation set.
#  3. tools/extract-player-frames.py: every player frame, from the ROM.
#  4. tools/make-enhanced-art.py: redraw, palettes, per-round pages, package.
#
# Needs the headless build, the profiling core (tools/build-profile-core.sh)
# and numpy + Pillow in build/tools-venv. Everything produced is derived from
# the ROM and stays under build/.
#   ART_STYLE=placeholder   outlined pixel-doubled frames, to check alignment
#   ART_OVERRIDE=dir        hand-made frames (<MAPPING>_c<KEY>.png)
#   SWEEP_FRAMES=36000      length of each round sweep
# Usage: tools/make-art-set.sh [rom]
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rom=${1:-${SOR_ROM:-"$root/original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md"}}
py="$root/build/tools-venv/bin/python3"; core="$root/build/gpgx-profile/genesis_plus_gx_libretro.dylib"
headless="$root/build/headless/sor-headless"
work="$root/build/art-set"; frames="$root/build/frames-set"
rm -rf "$work" "$frames" "$root/build/frames-players"; mkdir -p "$work" "$frames"
python3 - "$root" "$work" "${SWEEP_FRAMES:-36000}" <<'PY'
import json,sys
root,work,sweep=sys.argv[1],sys.argv[2],int(sys.argv[3])
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
# Round sweep: open-loop walking and punching in both lanes, a police special
# every sixth cycle. Infinite health and lives make survival certain; progress
# is whatever the punches and specials clear.
def tap(hold,n):
    return [s for _ in range(n) for s in ({'frames':3,'p1':hold+['B']},{'frames':7,'p1':hold})]
def cycle(ahead,back,special):
    c=[{'frames':50,'p1':[ahead]}]+tap([ahead],5)+[{'frames':25,'p1':[ahead,'UP']}]+tap([],4)+tap([back],3)
    c+=[{'frames':60,'p1':[ahead]},{'frames':25,'p1':[ahead,'DOWN']}]+tap([],4)+[{'frames':40,'p1':[ahead]}]
    return c+([{'frames':2,'p1':['A']},{'frames':30}] if special else [])
for name,ahead,back in (('sweep','RIGHT','LEFT'),('sweep-rtl','LEFT','RIGHT')):
    seg=variant('round1-full',None)[:10];n=i=0
    while n<sweep:
        c=cycle(ahead,back,i%6==5);seg+=c;n+=sum(s['frames'] for s in c);i+=1
    json.dump({'segments':seg},open(f'{work}/{name}.json','w'))
PY
for c in adam axel blaze; do
    "$py" "$root/tools/bot-play.py" "$core" "$rom" "$work/$c-prologue.json" 16000 "$work/$c-bot.json" --weaken > /dev/null &
    "$py" "$root/tools/bot-play.py" "$core" "$rom" "$work/$c-prologue.json" 16000 "$work/$c-bot-items.json" --weaken --throws --pickups > /dev/null &
done; wait
for scenario in "$work"/*.json; do
    name=$(basename "$scenario" .json); python3 "$root/tools/replay.py" "$scenario" "$work/$name.bin" > /dev/null
done
extract() { # name replay [round]
    mkdir -p "$frames/$1"
    if [ -n "${3:-}" ]; then
        SOR_EXTRACT_FRAMES="$frames/$1" SOR_CHEATS=$3 "$headless" "$rom" "$2" /dev/null > /dev/null 2>&1
    else
        SOR_EXTRACT_FRAMES="$frames/$1" "$headless" "$rom" "$2" /dev/null > /dev/null 2>&1
    fi
}
for replay in "$work"/*.bin; do
    name=$(basename "$replay" .bin)
    case $name in *-prologue|sweep|sweep-rtl) continue;; esac
    extract "$name" "$replay" &
done
for round in 1 2 3 4 5 6 7; do extract "round$round" "$work/sweep.bin" $round & done
extract round8 "$work/sweep-rtl.bin" 8 &
wait
"$py" "$root/tools/extract-player-frames.py" "$rom" "$root/build/frames-players" "$frames"/*
"$py" "$root/tools/make-enhanced-art.py" "$root/build/frames-players" "$frames"/* --out "$root/build/art/SORART.PAK" \
    --style "${ART_STYLE:-enhanced}" --sheet "$root/build/art/sheets" --frames "$root/build/art/frames" \
    ${ART_OVERRIDE:+--override "$ART_OVERRIDE"}
