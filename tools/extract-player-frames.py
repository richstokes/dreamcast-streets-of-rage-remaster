#!/usr/bin/env python3
"""Every animation frame of the three player characters, straight from the ROM.

Replays only show the frames a run happens to draw. This walks each player's
animation set (graphics-engine analysis, section 8.3):

  set:       word[i] -> animation record, relative to the set
  animation: byte frames, byte duration, word[f] -> frame record, relative to
             the animation (bit 15: the mirrored decoding of the same record)
  frame:     byte pieces-1, 2 box ids (unmirrored), 2 box ids (mirrored),
             byte upper art id, byte lower art id, then 5-byte pieces
             (s8 y, size, word tile attributes, s8 x)

Player tiles are not resident: the art ids select ROM-to-VRAM DMA records
($1A160 upper -> VRAM $B000, $1A53E lower -> $B400 for player 1), which are
replayed here. Output matches SOR_EXTRACT_FRAMES (src/headless/
extract_frames.cpp): <mapping>_c<colour key>.pam and index.json, with the
mapping being the frame record's address. Colours come from REFERENCE
directories of replay-extracted frames (the CRAM line each character was seen
with; the game builds its palettes in RAM); frames also present there are
compared pixel for pixel.

  tools/extract-player-frames.py ROM OUT REFERENCE_DIR...
"""
import json, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from art_key import colour_key

SETS = {'adam': 0x53EFE, 'axel': 0x49AE0, 'blaze': 0x5E90A}
UPPER, LOWER = 0x1A160, 0x1A53E


def word(rom, a): return struct.unpack_from('>H', rom, a)[0]
def s8(v): return v - 256 if v > 127 else v


def dma(rom, table, art, length_words):
    r = rom[table + art * 6: table + art * 6 + 6]
    assert r[0] == 0x95 and r[2] == 0x96 and r[4] == 0x97, r.hex()
    source = (r[5] << 16 | r[3] << 8 | r[1]) * 2
    return rom[source: source + length_words * 2]


def frames_of(rom, base):
    """{frame record address: animation index} for unmirrored references."""
    found = {}
    for i in range(word(rom, base) // 2):
        anim = base + word(rom, base + i * 2)
        for f in range(rom[anim]):
            offset = word(rom, anim + 2 + f * 2)
            found.setdefault(anim + (offset & 0x7FFF), i)
    return found


def render(rom, record):
    """(width, height, anchor, palette, index rows) of a frame record, or None."""
    count = rom[record]
    if count > 127:
        return None
    upper, lower = rom[record + 5], rom[record + 6]
    vram = bytearray(0x10000)
    if upper: vram[0xB000:0xB000 + 0x440] = dma(rom, UPPER, upper, 0x220)
    if lower: vram[0xB400:0xB400 + 0x400] = dma(rom, LOWER, lower, 0x200)
    pieces = []
    for p in range(count + 1):
        y, size, hi, lo, x = rom[record + 7 + p * 5: record + 12 + p * 5]
        pieces.append((s8(x), s8(y), ((size >> 2 & 3) + 1) * 8, ((size & 3) + 1) * 8, hi << 8 | lo))
    x0 = min(p[0] for p in pieces); y0 = min(p[1] for p in pieces)
    x1 = max(p[0] + p[2] for p in pieces); y1 = max(p[1] + p[3] for p in pieces)
    w, h = x1 - x0, y1 - y0
    rows = [[0] * w for _ in range(h)]
    for x, y, pw, ph, attr in pieces:            # earlier pieces are in front
        hf, vf, tile, cells = attr & 0x800, attr & 0x1000, attr & 2047, ph // 8
        for py in range(ph):
            for px in range(pw):
                sx, sy = (pw - 1 - px if hf else px), (ph - 1 - py if vf else py)
                a = ((tile + (sx // 8) * cells + sy // 8) * 32 + (sy & 7) * 4 + (sx & 7) // 2) & 0xFFFF
                index = vram[a] & 15 if sx & 1 else vram[a] >> 4
                if index and not rows[y + py - y0][x + px - x0]:
                    rows[y + py - y0][x + px - x0] = index
    return w, h, (-x0, -y0), pieces[0][4] >> 13 & 3, rows


def load_pam(path):
    header, data = path.read_bytes().split(b'ENDHDR\n', 1)
    return data


def main():
    rom = Path(sys.argv[1]).read_bytes(); out = Path(sys.argv[2]); refs = [Path(p) for p in sys.argv[3:]]
    out.mkdir(parents=True, exist_ok=True)
    reference = {}
    for d in refs:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            reference.setdefault((int(f['mapping'], 16), tuple(f['cram'][1:])), (f, d))
    index, report = [], {}
    for name, base in SETS.items():
        records = frames_of(rom, base)
        rendered = {r: v for r, v in ((r, render(rom, r)) for r in sorted(records)) if v}
        # The colours the character was seen in (a second player of the same
        # character gets its own): each is one CRAM line, from the replays.
        # Steps of a fade last a few frames each; a real look is held.
        lines, held = {}, {}
        for d in refs:
            for f in json.loads((d / 'index.json').read_text())['frames']:
                if int(f['mapping'], 16) in rendered and 1 in f['types'] and any(f['cram'][1:]):
                    held[f['colours']] = max(held.get(f['colours'], 0), f['seen']); lines[f['colours']] = f['cram']
        lines = {c: cram for c, cram in lines.items() if held[c] >= 90}
        # The character's colour mask: every CRAM entry any of its frames uses.
        mask = 0
        for w, h, anchor, palette, rows in rendered.values():
            for row in rows:
                for i in set(row):
                    mask |= 1 << i
        mask &= 0xFFFE
        lines = {'%04X' % colour_key(cram, mask): cram for cram in lines.values()}
        same = differ = 0
        for r, (w, h, anchor, palette, rows) in rendered.items():
            for colours, cram in lines.items():
                table = [bytes(((c >> s & 7) * 255 // 7) for s in (1, 5, 9)) for c in cram]
                rgba = bytearray(w * h * 4)
                for y in range(h):
                    for x in range(w):
                        if rows[y][x]:
                            rgba[(y * w + x) * 4:(y * w + x) * 4 + 4] = table[rows[y][x]] + b'\xff'
                if (r, tuple(cram[1:])) in reference:
                    f, d = reference[(r, tuple(cram[1:]))]
                    ok = (f['width'], f['height']) == (w, h) and load_pam(d / ('%06X_c%s.pam' % (r, f['colours']))) == bytes(rgba)
                    same += ok; differ += not ok
                (out / ('%06X_c%s.pam' % (r, colours))).write_bytes(
                    b'P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n' % (w, h) + bytes(rgba))
                index.append(dict(mapping='%06X' % r, colours=colours, mask=mask, palette=palette, cram=cram, width=w, height=h,
                                  anchor=list(anchor), seen=1, first_frame=0, variants=1, check='rom', rounds=0,
                                  types=[1], character=name, animation=records[r]))
        report[name] = dict(frames=len(rendered), colour_sets=sorted(lines), match_replays=same, differ_from_replays=differ)
    (out / 'index.json').write_text(json.dumps({'frames': index}, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
