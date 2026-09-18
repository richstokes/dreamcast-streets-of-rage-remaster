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
    ap.add_argument('--elf', type=Path, help='defaults to <log name>.elf kept by bench-flycast.sh')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--by-file', action='store_true', help='aggregate by source file instead of function')
    args = ap.parse_args()
    if args.elf is None:
        kept = args.log.with_name(args.log.name.replace('-flycast.log', '.elf'))
        args.elf = kept if kept.exists() else ROOT / 'build/native/sor.elf'
    text = args.log.read_text(errors='replace')
    totals = {m[0]: int(m[1]) for m in re.findall(r'PCPROF phase=(\w+) samples=(\d+)', text)}
    bins = [(int(p), int(a, 16), int(n)) for p, a, n in re.findall(r'^PCPROF (\d) ([0-9a-f]{8}) (\d+)$', text, re.M)]
    if not bins:
        ap.error('No PCPROF samples; build with SOR_PC_PROFILE=1 and let the replay finish.')
    addr2line = os.environ.get('ADDR2LINE', str(Path.home() / '.local/share/dreamcast/sh-elf/bin/sh-elf-addr2line'))
    addresses = sorted({a for _, a, _ in bins})
    out = subprocess.run([addr2line, '-f', '-C', '-e', str(args.elf)] + [hex(a) for a in addresses],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    if args.by_file:
        names = {a: Path(out[i * 2 + 1].split(':')[0]).name for i, a in enumerate(addresses)}
    else:
        names = {a: out[i * 2] for i, a in enumerate(addresses)}
    for phase, label in ((2, 'overrun'), (1, 'gameplay'), (0, 'other')):
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
