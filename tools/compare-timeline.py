#!/usr/bin/env python3
"""Tabulate frames spent in each game mode (game_state) by two traces of one replay."""
import argparse, json
from pathlib import Path


def timeline(directory):
    out, previous = [], None
    for line in open(directory / 'trace.jsonl'):
        row = json.loads(line)
        if row['mode'] != previous:
            out.append([row['mode'], row['frame']]); previous = row['mode']
    return [(m, f, (out[i + 1][1] - f) if i + 1 < len(out) else None) for i, (m, f) in enumerate(out)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original', type=Path); ap.add_argument('native', type=Path)
    a = ap.parse_args()
    g, n = timeline(a.original), timeline(a.native)
    print('mode   original(first,frames)   native(first,frames)   difference')
    for (m, gf, gl), (nm, nf, nl) in zip(g, n):
        diff = '' if gl is None or nl is None else f'{nl - gl:+d}'
        print(f'${m:02x}    {gf:6d} {str(gl):>5}            {nf:6d} {str(nl):>5}          {diff}' + ('' if m == nm else f'  MODE {nm:#x}'))


if __name__ == '__main__':
    main()
