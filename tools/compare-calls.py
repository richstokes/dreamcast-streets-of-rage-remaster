#!/usr/bin/env python3
"""Compare routine-entry timelines from the original ROM and the native port.

Inputs are `pc time` lines (time in master clocks since power-on) written by
genesis_reference.py --watch and by the host build with SOR_WATCH. The two call
sequences are aligned in order; the report lists where the native time drifts
from the original (native minus original, in 68000 cycles) between consecutive
matched calls, and where the sequences diverge.

  tools/compare-calls.py original.txt native.txt [--step 300] [--labels]
"""
import argparse, bisect
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def labels():
    rows = []
    for line in open(ROOT / 'research/StreetsOfRageProject/StreetsOfRageRecompilation/code-analysis/labels.csv'):
        if line.startswith('#') or ',' not in line: continue
        address, name = line.split(',', 2)[:2]
        try: rows.append((int(address, 16), name.strip()))
        except ValueError: pass
    return dict(rows)


def read(path):
    return [(int(pc, 16), int(t)) for pc, t in (l.split() for l in open(path) if l.strip())]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('original'); ap.add_argument('native')
    ap.add_argument('--step', type=int, default=300, help='report drift changes of at least this many cycles')
    ap.add_argument('--window', type=int, default=200, help='calls searched ahead to resynchronise')
    ap.add_argument('--frames', action='store_true', help='report only calls made in a different VBlank period')
    a = ap.parse_args()
    names = labels()
    name = lambda pc: names.get(pc, '%06x' % pc)
    g, n = read(a.original), read(a.native)
    i = j = 0; last = None; lastCall = None
    while i < len(g) and j < len(n):
        if g[i][0] == n[j][0]:
            drift = (n[j][1] - g[i][1]) // 7
            if a.frames:
                # VBlank periods since power-on (the first VINT is 112,644 clocks in).
                shift = (n[j][1] - 112644) // 896040 - (g[i][1] - 112644) // 896040
                if shift != last:
                    print('frame %5d  %-40s VBlank period %+d (drift %+d cycles)  since %s' % (
                        (g[i][1] - 112644) // 896040, name(g[i][0]), shift, drift, lastCall or 'start'))
                    last = shift
                lastCall = name(g[i][0]); i += 1; j += 1
                continue
            if last is None or abs(drift - last) >= a.step:
                frame = g[i][1] // 896040
                print('frame %5d  %-40s drift %+9d cycles (%+.2f frames)  since %s' % (
                    frame, name(g[i][0]), drift, drift / 128005.7, lastCall or 'start'))
                last = drift
            lastCall = name(g[i][0]); i += 1; j += 1
            continue
        # Resynchronise on the nearest call present in both (fewest skipped).
        best = None
        for d in range(1, a.window):
            for k in range(d + 1):
                if i + k < len(g) and j + d - k < len(n) and g[i + k][0] == n[j + d - k][0]:
                    best = (k, d - k); break
            if best: break
        if best is None:
            print('sequences diverge at original #%d %s / native #%d %s' % (i, name(g[i][0]), j, name(n[j][0])))
            return
        if best[0]: print('  original only: %s' % ', '.join(name(pc) for pc, _ in g[i:i + best[0]][:8]))
        if best[1]: print('  native only:   %s' % ', '.join(name(pc) for pc, _ in n[j:j + best[1]][:8]))
        i += best[0]; j += best[1]
    print('matched to original #%d of %d, native #%d of %d' % (i, len(g), j, len(n)))


if __name__ == '__main__':
    main()
