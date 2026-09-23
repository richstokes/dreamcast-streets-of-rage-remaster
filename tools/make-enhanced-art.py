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
  3. Colour: 8-bit PowerVR textures; each palette is quantised from its
     frames without dithering.
  4. Frames are cropped to their opaque pixels and the pages stored
     zlib-compressed (SORART06).

Frames are keyed by mapping address and colour key (art_catalog.hpp): the same
enemy frame under different colours is separate art. Pages are grouped by the
rounds their frames were seen in (index.json "rounds"; players and frames seen
in five or more rounds are always resident) and the Dreamcast loads a round's
pages only; the report lists PowerVR bytes per round. Three palettes of 255
colours: players, always-resident others, round-specific others.

Hand-made art overrides the generated frame: put <MAPPING>_c<KEY>.png (RGBA,
twice the extracted frame's size, same anchor) in an --override directory.

In-between poses for smooth animation (--inbetweens DIR, from
tools/make-inbetweens.py) are frames with a `from` mapping: shown for a few
ticks when an object goes from that pose to the frame's. Those the generator
rejected are left out unless an override <FROM>_<MAPPING>_c<KEY>.png exists.
They go on pages of their own, last in the package, loaded only while smooth
animation is on and only into memory the ordinary art leaves free.
--style placeholder makes pixel-doubled frames with a cyan outline and a
magenta anchor cross instead, for checking alignment. --sheet writes before/after comparison sheets for review.
Everything here is derived from the ROM: keep it under build/, out of git.

  tools/make-enhanced-art.py DIR... --out build/art/SORART.PAK [--sheet DIR]
"""
import argparse, json, struct, zlib
from pathlib import Path
import numpy as np
from PIL import Image

WORLD_TYPES = set(range(0x01, 0x60)) | set(range(0x90, 0xA0))
GUTTER = 2      # transparent texels around each frame (the PowerVR samples past a frame when art is scaled 480/448)


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



WEIGHT = np.float32([0.55, 0.77, 0.32])   # perceptual channel weights: green counts most, blue least

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
            diff = ((other - image) * WEIGHT) ** 2
            wgt = np.exp(-(dy * dy + dx * dx) / (2 * sigma_space ** 2) - diff.sum(-1) / (2 * sigma_colour ** 2))
            wgt *= mask[radius + dy: radius + dy + h, radius + dx: radius + dx + w]
            total += other * wgt[..., None]; weight += wgt
        image = np.where(alpha[..., None], total / np.maximum(weight, 1e-6)[..., None], image)
    out = rgba.copy(); out[..., :3] = np.clip(image + 0.5, 0, 255).astype(np.uint8)
    return out

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


def placeholder(rgba, anchor):
    """Pixel-doubled frame with a cyan outline and a magenta cross at the anchor."""
    big = np.repeat(np.repeat(rgba, 2, 0), 2, 1)
    big = np.pad(big, ((1, 1), (1, 1), (0, 0)))
    solid = big[..., 3] > 0
    near = np.zeros_like(solid)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            near |= np.roll(np.roll(solid, dy, 0), dx, 1)
    big[near & ~solid] = (0, 255, 255, 255)
    ax, ay = anchor[0] * 2 + 1, anchor[1] * 2 + 1
    for d in range(-3, 4):
        for x, y in ((ax + d, ay), (ax, ay + d)):
            if 0 <= x < big.shape[1] and 0 <= y < big.shape[0]:
                big[y, x] = (255, 0, 255, 255)
    return big, (ax, ay)


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
    ap.add_argument('--style', choices=('enhanced', 'placeholder'), default='enhanced')
    ap.add_argument('--override', type=Path, action='append', default=[], help='directory of hand-made frame PNGs')
    ap.add_argument('--inbetweens', type=Path, action='append', default=[], help='directory from tools/make-inbetweens.py')
    ap.add_argument('--sheet', type=Path, help='write before/after comparison sheets here')
    ap.add_argument('--frames', type=Path, help='write every generated frame as a PNG here (templates for hand-made art)')
    ap.add_argument('--min-seen', type=int, default=8, help='frames some frame of a look must have been on screen (in one run) for the look to get art')
    ap.add_argument('--passes', type=int, default=2)
    ap.add_argument('--sigma-space', type=float, default=1.6)
    ap.add_argument('--sigma-colour', type=float, default=21.0)
    a = ap.parse_args()

    frames, rounds = {}, {}
    for d in a.dirs:
        index = json.loads((d / 'index.json').read_text())['frames']
        # Whole-set renderings read an object's tiles from VRAM. An object whose
        # art is streamed (the round 3 boss) has other frames' tiles there, and
        # its set rendering contradicts the direct captures: its set-only frames
        # are scrambled and are not used (the original pieces draw instead).
        unreliable = {t for f in index if f.get('check') == 'contradicted' for t in f['types']}
        for f in index:
            if not set(f['types']) & WORLD_TYPES:
                continue
            check = f.get('check')
            if check == 'set_only' and set(f['types']) & unreliable:
                continue
            key = (int(f['mapping'], 16), f['colours'], 0)
            rounds[key] = rounds.get(key, 0) | f.get('rounds', 0)
            # The ROM extraction (players) wins; then direct captures and
            # confirmed set renderings; then the rest, by how often seen.
            rank = (3 if 'character' in f else 2 if check in ('confirmed', 'direct') else 1 if check in ('set_only', 'direct_culled') else 0, f['seen'])
            if key not in frames or rank > frames[key][2]:
                frames[key] = (f, d, rank)
    # Fades and flashes are not an object's look. A step of a fade is another
    # look of the same frame with every colour darker (or, flashing, lighter),
    # seen for less time; a look barely seen at all is dropped too. They get no
    # art, and the game's own pieces are drawn for them.
    shown, looks = {}, {}
    for d in a.dirs:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            shown[f['colours']] = max(shown.get(f['colours'], 0), f['seen'])
            channels = [c >> s & 7 for i, c in enumerate(f['cram']) if f['mask'] >> i & 1 for s in (1, 5, 9)]
            looks.setdefault(int(f['mapping'], 16), {})[f['colours']] = channels
    dropped = {c for c, n in shown.items() if n < a.min_seen}
    for same in looks.values():
        for ka, ca in same.items():
            for kb, cb in same.items():
                if ka != kb and len(ca) == len(cb) and shown[kb] > shown[ka] and (
                        all(x <= y for x, y in zip(ca, cb)) or all(x >= y for x, y in zip(ca, cb))):
                    dropped.add(ka)
    dropped -= {f['colours'] for f, _, _ in frames.values() if 'character' in f}
    frames = {k: v for k, v in frames.items() if k[1] not in dropped}
    def file_name(f):
        return ('%s_' % f['from'] if 'from' in f else '') + '%s_c%s' % (f['mapping'], f['colours'])
    # In-betweens: both poses must have art, and the generator or a hand must have made one.
    for d in a.inbetweens:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            key = (int(f['mapping'], 16), f['colours'], int(f['from'], 16))
            if (key[0], key[1], 0) not in frames or (key[2], key[1], 0) not in frames:
                continue
            if f['kept'] or any((o / (file_name(f) + '.png')).exists() for o in a.override):
                frames[key] = (f, d, None); rounds[key] = rounds[(key[0], key[1], 0)] & rounds[(key[2], key[1], 0)] or rounds[(key[0], key[1], 0)]
    keys = sorted(frames, key=lambda k: (k[2] != 0, k))
    CHARACTERS = {'adam': 1, 'axel': 2, 'blaze': 3}
    def group(key):
        """(rounds mask, palette, character, in-between): what is loaded together."""
        between = 0x80 if key[2] else 0
        if 'character' in frames[key][0]:
            return 0xFF, 0, CHARACTERS[frames[key][0]['character']], between
        mask = rounds[key] & 0xFF
        if not mask or mask == 0xFF:
            return 0xFF, 1, 0, between
        return mask, 2, 0, between
    oversized = []
    images, anchors, sources, overridden = [], [], [], 0
    for key in keys:
        f, d, _ = frames[key]
        name = file_name(f)
        source = load_pam(d / (name + '.pam')); sources.append(source)
        art, anchor = None, (f['anchor'][0] * 2, f['anchor'][1] * 2)
        for o in a.override:
            if (o / (name + '.png')).exists():
                art = np.array(Image.open(o / (name + '.png')).convert('RGBA'))
                if art.shape[:2] != (source.shape[0] * 2, source.shape[1] * 2):
                    raise SystemExit('%s: override must be %dx%d' % (name, source.shape[1] * 2, source.shape[0] * 2))
                art[..., 3] = np.where(art[..., 3] >= 128, 255, 0); overridden += 1
        if art is None and a.style == 'placeholder':
            art, anchor = placeholder(source, f['anchor'])
        elif art is None:
            art = smooth(redraw_edges(source), a.passes, a.sigma_space, a.sigma_colour)
        if a.frames:
            a.frames.mkdir(parents=True, exist_ok=True); Image.fromarray(art).save(a.frames / (name + '.png'))
        # Crop to the opaque pixels (pieces are whole 8-pixel cells, mostly
        # empty at the edges); the anchor moves with the crop.
        ys, xs = np.nonzero(art[..., 3]); ax, ay = anchor
        if len(ys):
            art = art[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]; ax -= int(xs.min()); ay -= int(ys.min())
        else:
            art = art[:1, :1]
        if art.shape[0] + GUTTER > a.page or art.shape[1] + GUTTER > a.page:
            oversized.append(key); sources.pop(); continue      # wider than a page (cutscene pictures): left to the original
        images.append(art); anchors.append((ax, ay))

    keys = [k for k in keys if k not in set(oversized)]
    # Palettes: farthest-point seeding + k-means over 15-bit colours; no dithering.
    groups = [group(k) for k in keys]
    palettes, indexed, error_sum, error_n, error_max = [], [None] * len(keys), 0.0, 0, 0.0
    for bank in range(3):
        members = [i for i, g in enumerate(groups) if g[1] == bank]
        if not members:
            palettes.append(np.zeros((0, 3), np.uint16)); continue
        opaque = np.concatenate([images[i][images[i][..., 3] > 0][:, :3] for i in members])
        colours, counts = np.unique(to555(opaque) << 3 | 4, axis=0, return_counts=True)
        if len(colours) > 255:
            palette = np.unique(to555(quantise(colours.astype(np.float32), counts, 255).astype(np.uint8)), axis=0)
        else:
            palette = np.unique(to555(colours.astype(np.uint8)), axis=0)
        palettes.append(palette)
        pal8 = palette.astype(np.float32) * 8 + 4
        lut = {}
        for i in members:
            im = images[i]
            c = to555(im[..., :3]); flat = (c[..., 0].astype(np.int32) << 10 | c[..., 1] << 5 | c[..., 2]).ravel()
            out = np.zeros(flat.shape, np.uint8)
            for v in np.unique(flat):
                if v not in lut:
                    rgb = np.float32([(v >> 10) * 8 + 4, (v >> 5 & 31) * 8 + 4, (v & 31) * 8 + 4])
                    lut[v] = int(np.argmin((((pal8 - rgb) * WEIGHT) ** 2).sum(1))) + 1
                out[flat == v] = lut[v]
            out = out.reshape(im.shape[:2]); out[im[..., 3] == 0] = 0
            indexed[i] = out
            if (out > 0).any():
                e = np.abs(pal8[out[out > 0] - 1] - im[out > 0][:, :3])
                error_sum += float(e.sum()); error_n += e.size; error_max = max(error_max, float(e.max()))

    # Pages per group; a group's last page is as short as a power of two allows.
    pages, places = [], [None] * len(keys)          # pages: (height, rounds, bank, indices)
    # Most important first (the Dreamcast loads in this order until memory runs
    # out): players, then groups by how long their frames were on screen.
    weight = {}
    for k, g in zip(keys, groups):
        # Bosses count for more than their time on screen in the sweeps, where
        # they fall to one hit.
        boss = any(0x30 <= t < 0x60 for t in frames[k][0]['types'])
        weight[g] = weight.get(g, 0) + max(frames[k][0]['seen'], 300 if boss else 0)
    for g in sorted(set(groups), key=lambda g: (g[3], g[2] == 0, -weight[g], g)):
        members = [i for i, x in enumerate(groups) if x == g]
        placed, count = pack([(indexed[i].shape[1], indexed[i].shape[0]) for i in members], a.page)
        sheets = np.zeros((count, a.page, a.page), np.uint8)
        for i, (p, x0, y0) in zip(members, placed):
            ix = indexed[i]
            sheets[p, y0 + 1: y0 + 1 + ix.shape[0], x0 + 1: x0 + 1 + ix.shape[1]] = ix
            places[i] = (len(pages) + p, x0, y0)
        for p in range(count):
            rows = np.nonzero(sheets[p].any(1))[0]
            used = int(rows.max()) + 2 if len(rows) else 2
            height = 64
            while height < used: height *= 2
            pages.append((height, g[0], g[1], sheets[p, :height], g[2] | g[3]))

    out = bytearray(b'SORART06' + struct.pack('<III', len(pages), len(keys), 3))
    for palette in palettes:
        words = [0] + [0x8000 | int(r) << 10 | int(g) << 5 | int(b) for r, g, b in palette]
        out += struct.pack('<256H', *(words + [0] * (256 - len(words))))
    for height, mask, bank, sheet, character in pages:
        packed = zlib.compress(sheet.tobytes(), 9)
        out += struct.pack('<HHHBBI', a.page, height, mask, bank, character, len(packed)) + packed
    for key, im, (ax, ay), (p, x0, y0) in zip(keys, images, anchors, places):
        # The look's CRAM line (an in-between has its pose's), for fades and flashes.
        line = frames[key][0].get('cram') or frames[(key[0], key[1], 0)][0]['cram']
        out += struct.pack('<IHHHHHHHhhI16H', key[0], int(key[1], 16), frames[key][0]['mask'], p, x0 + 1, y0 + 1, im.shape[1], im.shape[0], ax, ay, key[2], *line)
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_bytes(out)

    if a.sheet:
        # Before (pixel-doubled) above after (final palette), per character / object type.
        a.sheet.mkdir(parents=True, exist_ok=True)
        sheets = {}
        for i, key in enumerate(keys):
            f = frames[key][0]
            sheets.setdefault(('inbetween-' if key[2] else '') + (f.get('character') or 'type-%02x' % min(f['types'])), []).append(i)
        for name, members in sheets.items():
            cells = []
            for i in members[:96]:
                before = np.repeat(np.repeat(sources[i], 2, 0), 2, 1)
                pal8 = palettes[groups[i][1]].astype(np.float32) * 8 + 4
                after = np.zeros(indexed[i].shape + (4,), np.uint8); m = indexed[i] > 0
                after[m, :3] = pal8[indexed[i][m] - 1].astype(np.uint8); after[m, 3] = 255
                width = max(before.shape[1], after.shape[1])
                before = np.pad(before, ((0, 0), (0, width - before.shape[1]), (0, 0)))
                after = np.pad(after, ((0, 0), (0, width - after.shape[1]), (0, 0)))
                cells.append(np.concatenate([before, after], 0))
            hh = max(c.shape[0] for c in cells); ww = max(c.shape[1] for c in cells); cols = 12
            sheet = np.full(((len(cells) + cols - 1) // cols * hh, cols * ww, 4), (40, 44, 52, 255), np.uint8)
            for n, c in enumerate(cells):
                y, x = n // cols * hh, n % cols * ww
                region = sheet[y: y + c.shape[0], x: x + c.shape[1]]
                region[c[..., 3] > 0] = c[c[..., 3] > 0]
            Image.fromarray(sheet).convert('RGB').save(a.sheet / (name + '.png'))

    # One player: a third of the player pages; two different characters: two thirds.
    players = sum(a.page * p[0] for p in pages if p[4] & 0x7F and not p[4] & 0x80)
    per_round = {r + 1: sum(a.page * p[0] for p in pages if p[1] >> r & 1 and not p[4]) + players // 3 for r in range(8)}
    between = {r + 1: sum(a.page * p[0] for p in pages if p[1] >> r & 1 and p[4] & 0x80) for r in range(8)}
    report = dict(style=a.style, frames=len(keys), oversized_left_to_original=len(oversized), colour_sets=len(shown) - len(dropped), transient_colour_sets_dropped=len(dropped), player_frames=sum(1 for g in groups if g[1] == 0 and not g[3]), inbetweens=sum(1 for k in keys if k[2]), overridden=overridden,
                  pages=len(pages), packed_bytes=len(out), colours=[len(p) for p in palettes],
                  powervr_bytes_per_round_one_player=per_round, powervr_bytes_per_character=players // 3, powervr_bytes_inbetweens_per_round_all_players=between,
                  mean_channel_error=round(error_sum / max(1, error_n), 2), max_channel_error=error_max)
    # Where each round's memory goes: art texels by object type (largest first).
    by_type = {}
    for k, g, im in zip(keys, groups, images):
        if g[2] or g[3]: continue
        for r in range(8):
            if g[0] >> r & 1:
                t = '%02x' % min(frames[k][0]['types'])
                by_type.setdefault(r + 1, {}); by_type[r + 1][t] = by_type[r + 1].get(t, 0) + im.shape[0] * im.shape[1]
    report['texels_by_type_per_round'] = {r: dict(sorted(v.items(), key=lambda x: -x[1])[:8]) for r, v in by_type.items()}
    a.out.with_suffix('.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
