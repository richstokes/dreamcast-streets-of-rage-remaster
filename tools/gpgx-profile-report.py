#!/usr/bin/env python3
"""Summarize a Genesis Plus GX per-PC cycle histogram (genesis_reference.py --profile) by routine."""
import argparse, bisect, csv, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def labels():
    rows = []
    for line in open(ROOT / 'research/StreetsOfRageProject/StreetsOfRageRecompilation/code-analysis/labels.csv'):
        if line.startswith('#') or ',' not in line: continue
        address, name = line.split(',', 2)[:2]
        try: rows.append((int(address, 16), name.strip()))
        except ValueError: pass
    rows.sort(); return rows


def by_routine(path, table):
    data = Path(path).read_bytes(); counts = struct.unpack('<%dQ' % (len(data) // 8), data)
    starts = [a for a, _ in table]; totals = {}
    for index, cycles in enumerate(counts):
        if not cycles: continue
        pc = index * 2; i = bisect.bisect_right(starts, pc) - 1
        name = table[i][1] if i >= 0 else '?'
        totals[name] = totals.get(name, 0) + cycles
    return totals


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('profiles', nargs='+'); ap.add_argument('--frames', type=int, default=1); ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args(); table = labels()
    results = [by_routine(p, table) for p in a.profiles]
    names = sorted(set().union(*results), key=lambda n: -max(r.get(n, 0) for r in results))
    print('CPU cycles per frame (master/7)'.ljust(60) + ''.join(Path(p).stem.rjust(14) for p in a.profiles))
    print('TOTAL'.ljust(60) + ''.join(f'{sum(r.values()) / 7 / a.frames:14.0f}' for r in results))
    for n in names[:a.top]:
        print(n[:58].ljust(60) + ''.join(f'{r.get(n, 0) / 7 / a.frames:14.0f}' for r in results))


if __name__ == '__main__':
    main()
