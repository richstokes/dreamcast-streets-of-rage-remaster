#!/usr/bin/env python3
"""Build a placeholder replacement-art package from extracted object frames.

The enhanced renderer draws objects with replacement art at twice the original
resolution (src/render/art_catalog.hpp). Until real art exists, this makes a
package from the original frames (SOR_EXTRACT_FRAMES, src/headless/
extract_frames.cpp): each frame is doubled with Scale2x (--nearest: plain
pixel doubling), then
given a cyan outline and a magenta cross at its anchor, so alignment is easy to
check and nobody mistakes it for finished art. It is derived from the ROM:
keep it under build/, out of git.

It also reports texture memory for the frame set in the formats the PowerVR
offers, per object type: the numbers a real art set would have to fit.

  tools/make-placeholder-art.py EXTRACTED_DIR... --out build/art/SORART.PAK
"""
import argparse, json, struct
from pathlib import Path

OUTLINE = (0, 255, 255)
ANCHOR = (255, 0, 255)
WORLD_TYPES = set(range(0x01, 0x60)) | set(range(0x90, 0xA0))   # objects in the playfield


def load_pam(path):
    raw = path.read_bytes()
    header, data = raw.split(b'ENDHDR\n', 1)
    fields = dict(line.split(b' ', 1) for line in header.split(b'\n')[1:] if b' ' in line)
    w, h = int(fields[b'WIDTH']), int(fields[b'HEIGHT'])
    return w, h, data


def scale2x(src, fw, fh):
    """Scale2x (AdvMAME2x): doubles pixel art, rounding diagonal edges.
    Transparent pixels (None) take part like any colour."""
    at = lambda x, y: src[y][x] if 0 <= x < fw and 0 <= y < fh else None
    out = [[None] * (fw * 2) for _ in range(fh * 2)]
    for y in range(fh):
        for x in range(fw):
            b, d, e, f, hh = at(x, y - 1), at(x - 1, y), at(x, y), at(x + 1, y), at(x, y + 1)
            e0 = e1 = e2 = e3 = e
            if b != hh and d != f:
                e0 = d if d == b else e
                e1 = f if b == f else e
                e2 = d if d == hh else e
                e3 = f if hh == f else e
            out[y * 2][x * 2], out[y * 2][x * 2 + 1] = e0, e1
            out[y * 2 + 1][x * 2], out[y * 2 + 1][x * 2 + 1] = e2, e3
    return out


def placeholder(frame, data, smooth=True):
    """2x frame with a 1-pixel outline: (width, height, anchor, rgba rows)."""
    fw, fh = frame['width'], frame['height']
    w, h = fw * 2 + 2, fh * 2 + 2
    src = [[tuple(data[(y * fw + x) * 4:(y * fw + x) * 4 + 3]) if data[(y * fw + x) * 4 + 3] else None
            for x in range(fw)] for y in range(fh)]
    big = scale2x(src, fw, fh) if smooth else [[src[y // 2][x // 2] for x in range(fw * 2)] for y in range(fh * 2)]
    px = [[None] * w for _ in range(h)]
    for y in range(fh * 2):
        px[1 + y][1:1 + fw * 2] = big[y]
    for y in range(h):
        for x in range(w):
            if px[y][x] is None and any(0 <= y + dy < h and 0 <= x + dx < w and px[y + dy][x + dx] not in (None, OUTLINE)
                                        for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                px[y][x] = OUTLINE
    ax, ay = frame['anchor'][0] * 2 + 1, frame['anchor'][1] * 2 + 1
    for d in range(-3, 4):
        for x, y in ((ax + d, ay), (ax, ay + d)):
            if 0 <= x < w and 0 <= y < h:
                px[y][x] = ANCHOR
    return w, h, (ax, ay), px


GUTTER = 2   # transparent texels around each frame (one per side): the PowerVR
             # samples past a frame's edge when art is scaled (480 lines from
             # 448), and wraps at the page's edges


def pack(images, size):
    """Shelf packing, tallest first: [(page, x, y)] in input order."""
    order = sorted(range(len(images)), key=lambda i: -images[i][1])
    places, pages = [None] * len(images), []
    for i in order:
        w, h = images[i][0] + GUTTER, images[i][1] + GUTTER
        if w > size or h > size:
            raise SystemExit('frame %d larger than a %d page' % (i, size))
        for p, page in enumerate(pages):
            shelf = page[-1]
            if shelf[1] + w <= size:                       # current shelf
                places[i] = (p, shelf[1], shelf[0]); shelf[1] += w; break
            top = shelf[0] + shelf[2]
            if top + h <= size:                            # new shelf on this page
                page.append([top, w, h]); places[i] = (p, 0, top); break
        else:
            pages.append([[0, w, h]]); places[i] = (len(pages) - 1, 0, 0)
    return places, len(pages)


def argb1555(c):
    return 0 if c is None else 0x8000 | (c[0] >> 3) << 10 | (c[1] >> 3) << 5 | c[2] >> 3


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dirs', nargs='+', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--page', type=int, default=512)
    ap.add_argument('--types', help='comma-separated hex object types (default: playfield objects)')
    ap.add_argument('--nearest', action='store_true', help='double pixels instead of Scale2x')
    a = ap.parse_args()
    types = {int(t, 16) for t in a.types.split(',')} if a.types else WORLD_TYPES
    frames = {}
    for d in a.dirs:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            if not set(f['types']) & types:
                continue
            key = (int(f['mapping'], 16), f['palette'])
            if key not in frames or f['seen'] > frames[key][0]['seen']:
                frames[key] = (f, d)
    keys = sorted(frames)
    images = []
    for key in keys:
        f, d = frames[key]
        _, _, data = load_pam(d / ('%s_p%d.pam' % (f['mapping'], f['palette'])))
        images.append(placeholder(f, data, smooth=not a.nearest))
    places, pages = pack(images, a.page)
    pixels = [[[None] * a.page for _ in range(a.page)] for _ in range(pages)]
    for (w, h, _, px), (p, x0, y0) in zip(images, places):
        for y in range(h):
            pixels[p][y0 + 1 + y][x0 + 1:x0 + 1 + w] = px[y]
    # SORART02 (one palette, 8-bit pages) when the set has at most 255 colours.
    colours = sorted({argb1555(c) for page in pixels for row in page for c in row} - {0})
    if len(colours) <= 255:
        index = {c: i + 1 for i, c in enumerate(colours)}; index[0] = 0
        out = bytearray(b'SORART02' + struct.pack('<III', pages, len(keys), len(colours) + 1))
        out += struct.pack('<%dH' % (len(colours) + 1), 0, *colours)
        for page in pixels:
            out += struct.pack('<HH', a.page, a.page)
            out += bytes(index[argb1555(c)] for row in page for c in row)
    else:
        out = bytearray(b'SORART01' + struct.pack('<II', pages, len(keys)))
        for page in pixels:
            out += struct.pack('<HH', a.page, a.page)
            out += b''.join(struct.pack('<H', argb1555(c)) for row in page for c in row)
    for key, (w, h, (ax, ay), _), (p, x0, y0) in zip(keys, images, places):
        out += struct.pack('<IHHHHHHhh', key[0], key[1], p, x0 + 1, y0 + 1, w, h, ax, ay)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(out)

    # Budget report: texels by object type and bytes per PowerVR format.
    by_type = {}
    for key, (w, h, _, _) in zip(keys, images):
        for t in frames[key][0]['types']:
            if t in types:
                entry = by_type.setdefault(t, [0, 0]); entry[0] += 1; entry[1] += w * h
    texels = sum(w * h for w, h, _, _ in images)
    report = dict(frames=len(keys), pages=pages, page_size=a.page, packed_bytes=len(out), colours=len(colours),
                  texels=texels, page_fill=round(texels / (pages * a.page * a.page), 3),
                  bytes={'argb1555_pages': pages * a.page * a.page * 2, 'argb1555_tight': texels * 2,
                         'pal8_tight': texels, 'pal4_tight': texels // 2, 'vq_estimate': texels // 4 + 2048 * pages},
                  by_type={'%02x' % t: dict(frames=n, texels=px, argb1555_bytes=px * 2) for t, (n, px) in sorted(by_type.items())})
    a.out.with_suffix('.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('frames', 'pages', 'colours', 'packed_bytes', 'texels', 'page_fill', 'bytes')}))


if __name__ == '__main__':
    main()
