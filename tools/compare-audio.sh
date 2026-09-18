#!/bin/sh
# Compare native replay audio with the Genesis Plus GX capture after the gate.
# Usage: tools/compare-audio.sh <genesis-dir> <native-dir> <segment>
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
clang++ -std=c++20 -O3 "$root/tools/compare-audio.cpp" -o "$root/build/tests/compare-audio"
python3 - "$@" <<'PY' | xargs "$root/build/tests/compare-audio"
import json,sys
g,n,segment=sys.argv[1],sys.argv[2],int(sys.argv[3])
gm=json.load(open(g+'/metadata.json'))
gate=lambda d:[e['frame'] for e in json.load(open(d+'/events.json')) if e['segment']==segment][0]
frames=min(gm['frames']-gate(g),sum(1 for _ in open(n+'/trace.jsonl'))-1-gate(n))
print(g+'/audio.wav',gm['audio_sample_rate'],gm['audio_sample_frames'],gm['frames'],gate(g),n+'/audio.s16',gate(n),frames)
PY
