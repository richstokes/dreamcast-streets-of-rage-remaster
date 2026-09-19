#!/usr/bin/env python3
"""Fit per-event MC68000 costs for SoR's decompressors.

Training inputs are ROM offsets that decode cleanly (any valid stream exercises
the original routine exactly as the event counter predicts). Each is timed by
running the ROM's own routine in build/tests/m68k-time. The game's own calls
(build/logs/decoder-calls.txt) are held out as the test set.
Usage: tools/fit-decoder-cycles.py ROM
"""
import random, re, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from decoder_events import events

ROOT = Path(__file__).resolve().parents[1]
ENTRY = dict(nemesis=0x8192, kosinski=0x85a2, enigma=0x82d6)


def candidates(rom, family, limit, rng):
    found = []
    offsets = list(range(0x200, len(rom) - 64, 2)); rng.shuffle(offsets)
    for src in offsets:
        if len(found) >= limit: break
        try:
            if family == 'nemesis':
                tiles = ((rom[src] << 8) | rom[src + 1]) & 0x7fff
                if not 1 <= tiles <= 0x180: continue
            if family == 'enigma' and not (1 <= rom[src] <= 11 and rom[src + 1] < 0x20): continue
            ev, out = events(rom, ENTRY[family], src, 0)
        except (KeyError, IndexError, ValueError):
            continue
        if family == 'kosinski' and not 64 <= out <= 0x3000: continue
        if family == 'enigma' and not 16 <= out <= 0x1000: continue
        found.append((src, ev))
    return found


def measure(rom_path, rows):
    text = ''.join('%x %x 0 0\n' % (entry, src) for entry, src in rows)
    out = subprocess.run([str(ROOT / 'build/tests/m68k-time'), rom_path], input=text, capture_output=True, text=True, check=True).stdout
    result = {}
    for line in out.splitlines():
        e, s, c = line.split()[:3]
        if int(c) > 0: result[(int(e, 16), int(s, 16))] = int(c)
    return result


def least_squares(X, y):
    n = len(X[0])
    A = [[sum(x[p] * x[q] for x in X) + (1e-6 if p == q else 0) for q in range(n)] for p in range(n)]
    B = [sum(x[p] * v for x, v in zip(X, y)) for p in range(n)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(A[r][i])); A[i], A[p] = A[p], A[i]; B[i], B[p] = B[p], B[i]
        for j in range(i + 1, n):
            t = A[j][i] / A[i][i]; A[j] = [a - t * b for a, b in zip(A[j], A[i])]; B[j] -= t * B[i]
    w = [0.0] * n
    for i in reversed(range(n)): w[i] = (B[i] - sum(A[i][j] * w[j] for j in range(i + 1, n))) / A[i][i]
    return w


def main():
    rom_path = sys.argv[1]; rom = Path(rom_path).read_bytes()
    rng = random.Random(1)
    held = {}
    for line in open(ROOT / 'build/logs/decoder-calls.txt'):
        m = dict(re.findall(r'(\w+)=(\w+)', line)); held[(int(m['entry'], 16), int(m['src'], 16))] = int(m['d0'], 16)
    held_cycles = measure(rom_path, list(held))
    model = {}
    for family in ('nemesis', 'kosinski', 'enigma'):
        train = candidates(rom, family, 400, rng)
        cycles = measure(rom_path, [(ENTRY[family], s) for s, _ in train])
        train = [(s, ev) for s, ev in train if (ENTRY[family], s) in cycles]
        if family == 'enigma':
            # Random headers rarely resemble the game's 10-bit, 4-flag maps;
            # train on the game's own Enigma calls too (no Enigma hold-out).
            for (entry, src), d0 in held.items():
                if entry in (0x82d6, 0x82d2, 0x12832) and (entry, src) in held_cycles:
                    ev, _ = events(rom, entry, src, d0)
                    train.append((src, ev)); cycles[(ENTRY[family], src)] = held_cycles[(entry, src)]
        keys = [k for k in train[0][1] if k != 'header' and any(ev[k] for _, ev in train)]
        X = [[ev[k] for k in keys] + [1] for _, ev in train]; y = [cycles[(ENTRY[family], s)] for s, _ in train]
        w = least_squares(X, y)
        errors = [abs(sum(a * b for a, b in zip(w, x)) - v) for x, v in zip(X, y)]
        print(f'{family}: trained on {len(train)} inputs; max training error {max(errors):.0f} cycles')
        print('   ', {k: round(v, 2) for k, v in zip(keys + ['const'], w)})
        model[family] = (keys, w)
    print('held-out game calls:')
    worst = 0
    for (entry, src), d0 in sorted(held.items()):
        family = 'nemesis' if entry in (0x8192, 0x81a4) else 'kosinski' if entry == 0x85a2 else 'enigma'
        ev, _ = events(rom, entry, src, d0)
        keys, w = model[family]
        predicted = sum(a * ev.get(k, 0) for a, k in zip(w, keys)) + w[-1]
        actual = held_cycles.get((entry, src))
        if actual:
            worst = max(worst, abs(predicted - actual))
            print(f'  {entry:05x} {src:05x} actual {actual:8d} predicted {predicted:9.0f} error {predicted - actual:+7.0f}')
    print(f'worst held-out error {worst:.0f} cycles ({worst / 128005:.3f} frames)')
    print('COEFFICIENTS = {')
    for family, (keys, w) in model.items():
        print(f"    '{family}': {{" + ', '.join(f"'{k}': {v:.3f}" for k, v in zip(keys + ['const'], w)) + '},')
    print('}')


if __name__ == '__main__':
    main()
