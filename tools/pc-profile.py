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
    ap.add_argument('--elf', type=Path, help='defaults to adjacent sor-test.debug.elf or the ELF kept by bench-flycast.sh')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--by-file', action='store_true', help='aggregate by source file instead of function')
    args = ap.parse_args()
    if args.elf is None:
        hardware = args.log.with_name('sor-test.debug.elf')
        kept = args.log.with_name(args.log.name.replace('-flycast.log', '.elf'))
        if hardware.is_file():
            args.elf = hardware
        elif kept != args.log and kept.is_file():
            args.elf = kept
        elif args.log.name == 'console.log' or args.log.with_name('manifest.json').exists():
            ap.error('Hardware log needs its matching sor-test.debug.elf or explicit --elf; current workspace symbols may differ.')
        else:
            args.elf = ROOT / 'build/native/sor.elf'
    if args.elf.resolve() == args.log.resolve():
        ap.error('The log cannot also be the ELF symbol file.')
    with args.elf.open('rb') as elf:
        if elf.read(4) != b'\x7fELF':
            ap.error(f'Not an ELF symbol file: {args.elf}')
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
