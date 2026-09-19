#!/usr/bin/env python3
"""Check the native decompressors' emulated 68000 time against the ROM routines.

Runs a replay with the host build (SOR_DECODE_LOG=1 lists each decode's ROM time
before DRAM refresh) and times every distinct decode with tools/m68k-time running
the cartridge's own routine without refresh. Incremental Nemesis streams are
compared as a whole ($84BA and every $8510 call). All must match exactly.
Usage: tools/test-decoder-cycles.py ROM REPLAY.bin
Build first: tools/build-headless.sh and tools/m68k-time.sh.
"""
import os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def native_decodes(rom, replay):
    with tempfile.TemporaryDirectory() as tmp:
        run = subprocess.run([str(ROOT / 'build/headless/sor-headless'), rom, replay, os.path.join(tmp, 'ram.bin')],
                             env=dict(os.environ, SOR_DECODE_LOG='1', SOR_AUDIO='0'), capture_output=True, text=True)
    calls = [(int(e, 16), int(s, 16), int(c)) for e, s, c in
             re.findall(r'DECODE entry=(\w+) src=(\w+) cycles=(\d+)', run.stderr)]
    if not calls:
        raise SystemExit('no DECODE lines; is build/headless/sor-headless the SOR_PC_HISTOGRAM build?\n' + run.stderr[-2000:])
    expected = {}
    stream = None
    for entry, src, cycles in calls:
        if entry == 0x84BA:
            stream = [src, cycles]
            expected.setdefault((0x84BA, src), set()).add(None)
            continue
        if entry == 0x8510:
            if stream is None or stream[0] != src:
                raise SystemExit('incremental call without a started stream: %x' % src)
            stream[1] += cycles
            expected[(0x84BA, src)].add(stream[1])   # running totals; the last is the stream's
            continue
        if entry == 0x1061C:
            entry = 0x85A2   # the logged part is the driver's Kosinski decode
        if entry == 0x112C0:
            continue          # needs the level in RAM; its fixed part is not decoder time
        expected.setdefault((entry, src), set()).add(cycles)
    return expected


def rom_times(rom, keys):
    text = ''.join('%x %x ff0000 0\n' % key for key in keys)
    out = subprocess.run([str(ROOT / 'build/tests/m68k-time'), rom], input=text, capture_output=True, text=True,
                         env=dict(os.environ, M68K_NO_REFRESH='1'), check=True).stdout.split()
    return {(int(out[i], 16), int(out[i + 1], 16)): int(out[i + 2]) for i in range(0, len(out), 3)}


def main():
    rom, replay = sys.argv[1:3]
    expected = native_decodes(rom, replay)
    actual = rom_times(rom, sorted(expected))
    failures = 0
    for key in sorted(expected):
        native = expected[key] - {None}
        ok = actual[key] in native if key[0] == 0x84BA else native == {actual[key]}
        failures += not ok
        print('%s %05x %06x rom %8d native %s' % ('ok  ' if ok else 'FAIL', key[0], key[1], actual[key],
              max(native) if key[0] == 0x84BA and native else sorted(native)))
    print('%d decodes, %d mismatches' % (len(expected), failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
