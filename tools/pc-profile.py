#!/usr/bin/env python3
"""Resolve SOR_PC_PROFILE=1 serial samples to functions using the built ELF."""
import argparse
import collections
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    ap.add_argument('--elf', type=Path, default=ROOT / 'build/native/sor.elf')
    ap.add_argument('--top', type=int, default=25)
    args = ap.parse_args()
    text = args.log.read_text(errors='replace')
    totals = {m[0]: int(m[1]) for m in re.findall(r'PCPROF phase=(\w+) samples=(\d+)', text)}
    bins = [(int(p), int(a, 16), int(n)) for p, a, n in re.findall(r'^PCPROF (\d) ([0-9a-f]{8}) (\d+)$', text, re.M)]
    if not bins:
        ap.error('No PCPROF samples; build with SOR_PC_PROFILE=1 and let the replay finish.')
    addr2line = os.environ.get('ADDR2LINE', str(Path.home() / '.local/share/dreamcast/sh-elf/bin/sh-elf-addr2line'))
    addresses = sorted({a for _, a, _ in bins})
    out = subprocess.run([addr2line, '-f', '-C', '-e', str(args.elf)] + [hex(a) for a in addresses],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    names = {a: out[i * 2] for i, a in enumerate(addresses)}
    for phase, label in ((1, 'gameplay'), (0, 'other')):
        functions = collections.Counter()
        for p, a, n in bins:
            if p == phase:
                functions[names[a]] += n
        total = totals.get(label, 0) or 1
        listed = sum(functions.values())
        print(f'== {label}: {total} samples ({listed * 100 // total}% in the listed bins)')
        for name, n in functions.most_common(args.top):
            print(f'{n * 100 / total:6.2f}%  {name[:150]}')


if __name__ == '__main__':
    main()
