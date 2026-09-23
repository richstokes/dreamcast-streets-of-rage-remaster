#!/bin/sh
# Compare native replay audio with the Genesis Plus GX capture after the gate.
# Usage: tools/compare-audio.sh <genesis-dir> <native-dir> <segment>
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-c++}" -std=c++20 -O3 "$root/tools/compare-audio.cpp" -o "$root/build/tests/compare-audio"
python3 - "$root" "$@" <<'PY'
import json, subprocess, sys
from pathlib import Path
root, g, n, segment = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), int(sys.argv[4])
sys.path.insert(0, str(root / 'tools'))
from sor_ram import gate_frame
gm = json.loads((g / 'metadata.json').read_text())
frames = min(gm['frames'] - gate_frame(g, segment), sum(1 for _ in open(n / 'trace.jsonl')) - 1 - gate_frame(n, segment))
subprocess.run([str(root / 'build/tests/compare-audio'), str(g / 'audio.wav'), str(gm['audio_sample_rate']),
                str(gm['audio_sample_frames']), str(gm['frames']), str(gate_frame(g, segment)), str(n / 'audio.s16'),
                str(gate_frame(n, segment)), str(frames)], check=True)
PY
