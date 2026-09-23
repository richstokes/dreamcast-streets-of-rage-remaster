#!/usr/bin/env python3
"""Compare the 68k sound driver's per-frame channel state with an original-ROM trace.

SoR sequences music and effects on the 68000 (`sound_engine`, $72914). Its
workspace holds 48-byte channel blocks: music FM $F040-$F12F, music DAC $F130,
music PSG $F160-$F1EF, effect FM $F1F0-$F24F and effect PSG $F2B0-$F30F.
Music is compared at the frame offset where it started in each run, effects at
the replay gate. Equal blocks mean identical sequencing: notes, durations,
envelopes, pitch and effect priority on every compared frame.
"""
import argparse, collections, json, mmap
from pathlib import Path

from sor_ram import gate_frame

MUSIC = [('music FM%d' % i, 0xF040 + i * 0x30) for i in range(5)] + [('music DAC', 0xF130)] + \
        [('music PSG%d' % i, 0xF160 + i * 0x30) for i in range(3)]
EFFECTS = [('effect FM%d' % i, 0xF1F0 + i * 0x30) for i in range(2)] + [('effect PSG%d' % i, 0xF2B0 + i * 0x30) for i in range(2)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original', type=Path); ap.add_argument('native', type=Path)
    ap.add_argument('--segment', type=int, required=True)
    ap.add_argument('--frames', type=int, default=1300)
    a = ap.parse_args()
    gate = lambda d: gate_frame(d, a.segment)
    first = lambda d: json.load(open(d / 'metadata.json'))['ram_first_frame']
    maps = []
    for d in (a.original, a.native):
        f = open(d / 'ram.bin', 'rb'); maps.append((mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ), first(d)))
    def block(i, frame, address):
        m, base = maps[i]; o = (frame - base) * 65536 + address
        return m[o:o + 0x30] if 0 <= o and o + 0x30 <= len(m) else None
    og, ng = gate(a.original), gate(a.native)
    frames = range(og, og + a.frames)
    if block(0, frames[-1], 0) is None:
        raise SystemExit('the original run has fewer than %d frames after its gate' % a.frames)
    # Music offset: native frame = original frame + k, chosen by exact equality.
    def music_equal(k):
        return sum(all(block(0, f, s) == block(1, f + k, s) for _, s in MUSIC) for f in frames)
    k = max(range(ng - og - 300, ng - og + 301), key=music_equal)
    result = dict(segment=a.segment, original_gate=og, native_gate=ng, compared_frames=a.frames,
                  music_offset=k, effect_offset=ng - og, blocks={})
    for (name, s), offset in [(m, k) for m in MUSIC] + [(e, ng - og) for e in EFFECTS]:
        bad = collections.Counter(); equal = active = 0
        for f in frames:
            x, y = block(0, f, s), block(1, f + offset, s)
            active += bool(x[0] & 0x80)
            if x == y: equal += 1
            elif y is None: bad[-1] += 1                      # past the end of the native run
            else: bad.update(j for j in range(0x30) if x[j] != y[j])
        result['blocks'][name] = dict(equal=equal, active_frames=active, differing_bytes=dict(sorted(bad.items())))
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
