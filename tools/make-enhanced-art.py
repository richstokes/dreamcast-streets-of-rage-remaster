#!/usr/bin/env python3
"""Build the enhanced replacement-art package from extracted object frames.

Input: frame directories from tools/extract-player-frames.py (every player
frame, from the ROM) and SOR_EXTRACT_FRAMES (whatever a replay drew). Each
frame is redrawn at twice the resolution (src/render/art_catalog.hpp):

  1. Edges: Scale2x applied twice resolves diagonals and curves to quarter
     pixels; averaging back to 2x anti-aliases every interior edge and rounds
     the silhouette (which stays hard: PowerVR punch-through, 1-bit alpha).
  2. Shading: an edge-preserving (bilateral) filter, run on the doubled image,
     blends neighbouring tones of one material -- the 3-4 step colour ramps and
     checkerboard dithering of the 16-colour originals become continuous
     gradients -- while outlines and boundaries between materials, which differ
     strongly in colour, stay crisp. Transparent pixels never take part.
  3. Colour: all frames share one palette of at most 255 colours (8-bit
     PowerVR textures, palette bank 1), quantised without dithering.
  4. Frames are cropped to their opaque pixels and the pages stored
     zlib-compressed (SORART03): main RAM and the disc hold about 0.5 MB.

Hand-made art overrides the generated frame: put <MAPPING>_p<PALETTE>.png
(RGBA, twice the extracted frame's size, same anchor) in an --override
directory. --sheet writes before/after comparison sheets for review.
Everything here is derived from the ROM: keep it under build/, out of git.

  tools/make-enhanced-art.py DIR... --out build/art/SORART.PAK [--sheet DIR]
"""
import argparse, json, struct, zlib
from pathlib import Path
import numpy as np
from PIL import Image

WORLD_TYPES = set(range(0x01, 0x60)) | set(range(0x90, 0xA0))
GUTTER = 2      # transparent texels around each frame (see make-placeholder-art.py)


def load_pam(path):
    header, data = path.read_bytes().split(b'ENDHDR\n', 1)
    fields = dict(line.split(b' ', 1) for line in header.split(b'\n')[1:] if b' ' in line)
    w, h = int(fields[b'WIDTH']), int(fields[b'HEIGHT'])
    return np.frombuffer(data, np.uint8).reshape(h, w, 4).copy()


def epx(rgba):
    """Scale2x on whole pixels (transparent is a colour like any other)."""
    key = rgba.view(np.uint32).reshape(rgba.shape[:2])
    key = np.where(rgba[..., 3] == 0, 0, key)           # one transparent value
    p = np.pad(key, 1)
    e, b, h, d, f = p[1:-1, 1:-1], p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]
    go = (b != h) & (d != f)
    out = np.empty((key.shape[0] * 2, key.shape[1] * 2), np.uint32)
    out[0::2, 0::2] = np.where(go & (d == b), d, e)
    out[0::2, 1::2] = np.where(go & (b == f), f, e)
    out[1::2, 0::2] = np.where(go & (d == h), d, e)
    out[1::2, 1::2] = np.where(go & (h == f), f, e)
    return out.view(np.uint8).reshape(out.shape[0], out.shape[1], 4).copy()


def redraw_edges(rgba):
    """2x frame with anti-aliased interior edges: Scale2x applied twice (4x,
    diagonals and curves resolved to quarter pixels), then averaged back to 2x.
    Coverage of at least half keeps a texel opaque; colour averages only the
    opaque quarter-texels, so nothing blends with the background."""
    big = epx(epx(rgba)).astype(np.float32)
    h, w = big.shape[0] // 2, big.shape[1] // 2
    blocks = big.reshape(h, 2, w, 2, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, 4, 4)
    cover = blocks[..., 3] / 255
    colour = (blocks[..., :3] * cover[..., None]).sum(2) / np.maximum(cover.sum(2), 1e-6)[..., None]
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = np.clip(colour + 0.5, 0, 255); out[..., 3] = np.where(cover.sum(2) >= 2, 255, 0)
    out[out[..., 3] == 0] = 0
    return out


def smooth(rgba, passes, sigma_space, sigma_colour):
    """Bilateral filter over opaque pixels; alpha is unchanged."""
    alpha = rgba[..., 3] > 0
    image = rgba[..., :3].astype(np.float32)
    radius = 2
    offsets = [(dy, dx) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)]
    for _ in range(passes):
        padded = np.pad(image, ((radius, radius), (radius, radius), (0, 0)))
        mask = np.pad(alpha, radius)
        total = np.zeros_like(image); weight = np.zeros(image.shape[:2], np.float32)
        h, w = alpha.shape
        for dy, dx in offsets:
            other = padded[radius + dy: radius + dy + h, radius + dx: radius + dx + w]
            # Perceptual weighting: differences in green count most, blue least.
            diff = ((other - image) * np.float32([0.55, 0.77, 0.32])) ** 2
            wgt = np.exp(-(dy * dy + dx * dx) / (2 * sigma_space ** 2) - diff.sum(-1) / (2 * sigma_colour ** 2))
            wgt *= mask[radius + dy: radius + dy + h, radius + dx: radius + dx + w]
            total += other * wgt[..., None]; weight += wgt
        image = np.where(alpha[..., None], total / np.maximum(weight, 1e-6)[..., None], image)
    out = rgba.copy(); out[..., :3] = np.clip(image + 0.5, 0, 255).astype(np.uint8)
    return out


WEIGHT = np.float32([0.55, 0.77, 0.32])   # perceptual channel weights


def quantise(colours, counts, n):
    """n representative colours: farthest-point seeding, so rare tones (a red
    shoe stripe among thousands of skin texels) get their own entries, then
    weighted k-means."""
    points = colours * WEIGHT; weights = np.sqrt(counts).astype(np.float32)
    centres = [points[np.argmax(counts)]]
    distance = ((points - centres[0]) ** 2).sum(1)
    for _ in range(n - 1):
        centres.append(points[np.argmax(distance * np.minimum(weights, 8))])
        distance = np.minimum(distance, ((points - centres[-1]) ** 2).sum(1))
    centres = np.array(centres)
    for _ in range(12):
        nearest = np.argmin(((points[:, None, :] - centres[None]) ** 2).sum(2), 1)
        for k in range(n):
            members = nearest == k
            if members.any():
                centres[k] = (points[members] * weights[members, None]).sum(0) / weights[members].sum()
    return centres / WEIGHT


def pack(sizes, page):
    """Shelf packing, tallest first: [(page, x, y)] in input order."""
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    places, pages = [None] * len(sizes), []
    for i in order:
        w, h = sizes[i][0] + GUTTER, sizes[i][1] + GUTTER
        if w > page or h > page:
            raise SystemExit('frame %d larger than a %d page' % (i, page))
        for p, shelves in enumerate(pages):
            shelf = shelves[-1]
            if shelf[1] + w <= page:
                places[i] = (p, shelf[1], shelf[0]); shelf[1] += w; break
            top = shelf[0] + shelf[2]
            if top + h <= page:
                shelves.append([top, w, h]); places[i] = (p, 0, top); break
        else:
            pages.append([[0, w, h]]); places[i] = (len(pages) - 1, 0, 0)
    return places, len(pages)


def to555(rgb):
    return (rgb.astype(np.uint16) >> 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dirs', nargs='+', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--page', type=int, default=512)
    ap.add_argument('--override', type=Path, action='append', default=[], help='directory of hand-made frame PNGs')
    ap.add_argument('--sheet', type=Path, help='write before/after comparison sheets here')
    ap.add_argument('--frames', type=Path, help='write every generated frame as a PNG here (templates for hand-made art)')
    ap.add_argument('--passes', type=int, default=2)
    ap.add_argument('--sigma-space', type=float, default=1.6)
    ap.add_argument('--sigma-colour', type=float, default=21.0)
    a = ap.parse_args()

    frames = {}
    for d in a.dirs:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            if not set(f['types']) & WORLD_TYPES:
                continue
            key = (int(f['mapping'], 16), f['palette'])
            # Prefer the ROM extraction (complete frames) over replay captures.
            if key not in frames or 'character' in f or (f['seen'] > frames[key][0]['seen'] and 'character' not in frames[key][0]):
                frames[key] = (f, d)
    keys = sorted(frames)
    images, anchors, sources, overridden = [], [], [], 0
    for key in keys:
        f, d = frames[key]
        name = '%s_p%d' % (f['mapping'], f['palette'])
        source = load_pam(d / (name + '.pam')); sources.append(source)
        art = None
        for o in a.override:
            if (o / (name + '.png')).exists():
                art = np.array(Image.open(o / (name + '.png')).convert('RGBA'))
                if art.shape[:2] != (source.shape[0] * 2, source.shape[1] * 2):
                    raise SystemExit('%s: override must be %dx%d' % (name, source.shape[1] * 2, source.shape[0] * 2))
                art[..., 3] = np.where(art[..., 3] >= 128, 255, 0); overridden += 1
        if art is None:
            art = smooth(redraw_edges(source), a.passes, a.sigma_space, a.sigma_colour)
        # Crop to the opaque pixels (pieces are whole 8-pixel cells, mostly
        # empty at the edges); the anchor moves with the crop.
        ys, xs = np.nonzero(art[..., 3]); ax, ay = f['anchor'][0] * 2, f['anchor'][1] * 2
        if len(ys):
            art = art[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]; ax -= int(xs.min()); ay -= int(ys.min())
        else:
            art = art[:1, :1]
        images.append(art); anchors.append((ax, ay))
        if a.frames:
            a.frames.mkdir(parents=True, exist_ok=True); Image.fromarray(art).save(a.frames / (name + '.png'))

    # One palette for the whole set: median cut + k-means over 15-bit colours,
    # weighted by how many texels use each; no dithering.
    opaque = np.concatenate([im[im[..., 3] > 0][:, :3] for im in images])
    colours, counts = np.unique(to555(opaque) << 3 | 4, axis=0, return_counts=True)
    if len(colours) > 255:
        palette = to555(quantise(colours.astype(np.float32), counts, 255).astype(np.uint8))
        palette = np.unique(palette, axis=0)
    else:
        palette = np.unique(to555(colours.astype(np.uint8)), axis=0)
    pal8 = (palette.astype(np.float32) * 8 + 4)
    lut = {}
    def indices(im):
        c = to555(im[..., :3]); flat = (c[..., 0].astype(np.int32) << 10 | c[..., 1] << 5 | c[..., 2]).ravel()
        out = np.zeros(flat.shape, np.uint8)
        for v in np.unique(flat):
            if v not in lut:
                rgb = np.float32([(v >> 10) * 8 + 4, (v >> 5 & 31) * 8 + 4, (v & 31) * 8 + 4])
                lut[v] = int(np.argmin((((pal8 - rgb) * np.float32([0.55, 0.77, 0.32])) ** 2).sum(1))) + 1
            out[flat == v] = lut[v]
        out = out.reshape(im.shape[:2]); out[im[..., 3] == 0] = 0
        return out
    indexed = [indices(im) for im in images]
    error = max(float(np.abs(pal8[ix[ix > 0] - 1] - im[ix > 0][:, :3]).max()) for ix, im in zip(indexed, images) if (ix > 0).any())

    places, pages = pack([(im.shape[1], im.shape[0]) for im in images], a.page)
    sheets = np.zeros((pages, a.page, a.page), np.uint8)
    for ix, (p, x0, y0) in zip(indexed, places):
        sheets[p, y0 + 1: y0 + 1 + ix.shape[0], x0 + 1: x0 + 1 + ix.shape[1]] = ix
    words = [0] + [0x8000 | int(r) << 10 | int(g) << 5 | int(b) for r, g, b in palette]
    out = bytearray(b'SORART03' + struct.pack('<III', pages, len(keys), len(words)))
    out += struct.pack('<%dH' % len(words), *words)
    for p in range(pages):
        packed = zlib.compress(sheets[p].tobytes(), 9)
        out += struct.pack('<HHI', a.page, a.page, len(packed)) + packed
    for key, im, (ax, ay), (p, x0, y0) in zip(keys, images, anchors, places):
        out += struct.pack('<IHHHHHHhh', key[0], key[1], p, x0 + 1, y0 + 1, im.shape[1], im.shape[0], ax, ay)
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_bytes(out)

    if a.sheet:
        # Before (nearest 2x) above after, final palette applied, 4x for viewing.
        a.sheet.mkdir(parents=True, exist_ok=True)
        groups = {}
        for i, key in enumerate(keys):
            groups.setdefault(frames[key][0].get('character', 'others'), []).append(i)
        for name, members in groups.items():
            cells = []
            for i in members[:48]:
                before = np.repeat(np.repeat(sources[i], 2, 0), 2, 1)
                after = np.zeros_like(images[i]); m = indexed[i] > 0
                after[m, :3] = pal8[indexed[i][m] - 1].astype(np.uint8); after[m, 3] = 255
                # The source is uncropped; pad both to a common width.
                ww2 = max(before.shape[1], after.shape[1])
                before = np.pad(before, ((0, 0), (0, ww2 - before.shape[1]), (0, 0)))
                after = np.pad(after, ((0, 0), (0, ww2 - after.shape[1]), (0, 0)))
                cell = np.concatenate([before, after], 0)
                cells.append(cell)
            hh = max(c.shape[0] for c in cells); ww = max(c.shape[1] for c in cells); cols = 12
            sheet = np.full(((len(cells) + cols - 1) // cols * hh, cols * ww, 4), (40, 44, 52, 255), np.uint8)
            for n, c in enumerate(cells):
                y, x = n // cols * hh, n % cols * ww
                region = sheet[y: y + c.shape[0], x: x + c.shape[1]]
                region[c[..., 3] > 0] = c[c[..., 3] > 0]
            Image.fromarray(sheet).convert('RGB').save(a.sheet / (name + '.png'))

    texels = sum(im.shape[0] * im.shape[1] for im in images)
    report = dict(frames=len(keys), overridden=overridden, pages=pages, colours=len(palette), packed_bytes=len(out),
                  powervr_bytes=pages * a.page * a.page, texels=texels, max_channel_error=error)
    a.out.with_suffix('.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
