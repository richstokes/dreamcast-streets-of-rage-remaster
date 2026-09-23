#!/usr/bin/env python3
"""In-between frames for smoother animation (see docs/REMASTER.md).

The game's animations have 3-4 drawn poses each. For every pair of poses that
follow one another in an animation record, this makes one new pose halfway
between them, at the original resolution and in the original 16 colours, so
tools/make-enhanced-art.py can redraw it like any extracted frame.

  1. Both poses are laid on one canvas, aligned at the object's anchor.
  2. Dense correspondence in both directions: a cost volume over every
     displacement within --reach pixels (colour and silhouette mismatch,
     aggregated over a small and a large window, with a preference for short
     moves), winner takes all, then a median filter.
  3. Each output pixel looks half a displacement back into the first pose and
     half forward into the second. Where the two agree the pixel is theirs;
     where they do not, the more self-consistent direction wins. Pixels are
     copied, never blended: the in-between keeps the poses' colours and hard
     outlines. Single-pixel islands and holes are removed.
  4. `agreement` is the fraction of the pixels that differ between the poses
     on which both agree about the in-between. Pairs below --min-agreement change too much to interpolate (an arm
     appearing from behind the body): they get no in-between and the game cuts
     between the poses as it always has.

Animation records come from the ROM: the three player sets, and every other
set whose frames are all among the extracted mappings (sets have no table in
the ROM, so they are found by their shape and confirmed by their frames).

Output: <FROM>_<TO>_c<KEY>.pam for every pair (rejected ones too, as drafts
for hand-made in-betweens: tools/make-enhanced-art.py --override) and
index.json in the extractors' format, each entry with `from`, `agreement` and
`kept`; --sheet previews show first pose, in-between, second pose, with a red
bar over rejected ones. Derived from the ROM: keep under build/.

  tools/make-inbetweens.py ROM OUT DIR... [--sheet DIR] [--only adam]
"""
import argparse, json, struct
from pathlib import Path
import numpy as np
from PIL import Image

PLAYER_SETS = {'adam': 0x53EFE, 'axel': 0x49AE0, 'blaze': 0x5E90A}


def word(rom, a): return struct.unpack_from('>H', rom, a)[0]


def load_pam(path):
    header, data = path.read_bytes().split(b'ENDHDR\n', 1)
    fields = dict(line.split(b' ', 1) for line in header.split(b'\n')[1:] if b' ' in line)
    w, h = int(fields[b'WIDTH']), int(fields[b'HEIGHT'])
    return np.frombuffer(data, np.uint8).reshape(h, w, 4).copy()


def save_pam(path, rgba):
    h, w = rgba.shape[:2]
    path.write_bytes(b'P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n' % (w, h) + rgba.tobytes())


def animations(rom, base, known=None):
    """[[frame record address, ...], ...] of a set, or None if base is not one.
    Mirrored references (bit 15) are kept as negative addresses."""
    if base + 2 > len(rom): return None
    size = word(rom, base)
    if size < 2 or size & 1 or size > 512 or base + size > len(rom): return None
    out = []
    for i in range(size // 2):
        anim = base + word(rom, base + i * 2)
        if anim + 2 > len(rom): return None
        count = rom[anim]
        if count > 32 or anim + 2 + count * 2 > len(rom): return None
        frames = []
        for f in range(count):
            w = word(rom, anim + 2 + f * 2)
            address = anim + (w & 0x7FFF)
            if known is not None and address not in known: return None
            frames.append(-address if w & 0x8000 else address)
        out.append(frames)
    return out


def box(a, r):
    """Box sum of radius r over the last two axes (edge-padded)."""
    if r == 0: return a
    pad = [(0, 0)] * (a.ndim - 2) + [(r + 1, r), (r + 1, r)]
    c = np.pad(a, pad, mode='edge').cumsum(-1).cumsum(-2)
    n = 2 * r + 1
    return c[..., n:, n:] - c[..., :-n, n:] - c[..., n:, :-n] + c[..., :-n, :-n]


def shifted(a, dy, dx, fill=0):
    """a moved by (dy, dx): out[y, x] = a[y - dy, x - dx]."""
    out = np.full_like(a, fill)
    h, w = a.shape[:2]
    ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
    xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
    out[yd, xd] = a[ys, xs]
    return out


def flow(a, b, reach, prior):
    """Per pixel of a, the (dy, dx) to its counterpart in b."""
    h, w = a.shape[:2]
    fa = a.astype(np.float32); fb = b.astype(np.float32)
    best = np.full((h, w), np.inf, np.float32); field = np.zeros((h, w, 2), np.int16)
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            other = shifted(fb, -dy, -dx)                       # other[y, x] = b[y + dy, x + dx]
            d = fa - other
            # Silhouette mismatch costs as much as the largest colour difference.
            pixel = np.sqrt((d[..., :3] ** 2).sum(-1)) / 441 + 2 * np.abs(d[..., 3]) / 255
            cost = box(pixel, 2) / 25 + box(pixel, 6) / 169 + prior * np.hypot(dy, dx)
            better = cost < best
            best[better] = cost[better]; field[better] = (dy, dx)
    return median(field, 2)


def median(field, r):
    h, w = field.shape[:2]
    p = np.pad(field, ((r, r), (r, r), (0, 0)), mode='edge')
    stack = np.stack([p[y: y + h, x: x + w] for y in range(2 * r + 1) for x in range(2 * r + 1)])
    return np.median(stack, 0).astype(np.int16)


def sample(image, ys, xs):
    h, w = image.shape[:2]
    inside = (ys >= 0) & (ys < h) & (xs >= 0) & (xs < w)
    out = image[np.clip(ys, 0, h - 1), np.clip(xs, 0, w - 1)].copy()
    out[~inside] = 0
    return out


def halfway(a, b, field):
    """The two poses' candidates for each in-between pixel, from a's field."""
    h, w = a.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w]
    # The field is known at a's pixels; find it at the in-between's by fixed point.
    fy = np.zeros((h, w), np.float32); fx = np.zeros((h, w), np.float32)
    for _ in range(3):
        qy = np.clip(np.rint(ys - fy / 2), 0, h - 1).astype(int); qx = np.clip(np.rint(xs - fx / 2), 0, w - 1).astype(int)
        fy, fx = field[qy, qx, 0].astype(np.float32), field[qy, qx, 1].astype(np.float32)
    ay, ax = np.rint(ys - fy / 2).astype(int), np.rint(xs - fx / 2).astype(int)
    by, bx = np.rint(ys + fy / 2).astype(int), np.rint(xs + fx / 2).astype(int)
    return sample(a, ay, ax), sample(b, by, bx)


def same(p, q):
    return (p[..., 3] == q[..., 3]) & ((p[..., 3] == 0) | (np.abs(p[..., :3].astype(int) - q[..., :3]).sum(-1) < 48))


def despeckle(rgba):
    """Single-pixel islands go; single-pixel holes take their commonest neighbour."""
    h, w = rgba.shape[:2]
    solid = rgba[..., 3] > 0
    p = np.pad(solid, 1)
    around = sum(p[1 + dy: 1 + dy + h, 1 + dx: 1 + dx + w].astype(int) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx)
    out = rgba.copy()
    out[solid & (around <= 1)] = 0
    holes = ~solid & (around >= 7)
    pc = np.pad(rgba, ((1, 1), (1, 1), (0, 0)))
    for y, x in zip(*np.nonzero(holes)):
        near = [tuple(pc[y + 1 + dy, x + 1 + dx]) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy or dx) and pc[y + 1 + dy, x + 1 + dx, 3]]
        out[y, x] = max(set(near), key=near.count)
    # A pixel whose colour none of its 8 neighbours shares takes their commonest.
    pc = np.pad(out, ((1, 1), (1, 1), (0, 0)))
    key = pc.view(np.uint32).reshape(pc.shape[:2])
    alone = np.ones((h, w), bool)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dy or dx: alone &= key[1 + dy: 1 + dy + h, 1 + dx: 1 + dx + w] != key[1:-1, 1:-1]
    for y, x in zip(*np.nonzero(alone & (out[..., 3] > 0))):
        near = [tuple(pc[y + 1 + dy, x + 1 + dx]) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy or dx) and pc[y + 1 + dy, x + 1 + dx, 3]]
        if near: out[y, x] = max(set(near), key=near.count)
    return out


def inbetween(a, b, reach, prior):
    fab, fba = flow(a, b, reach, prior), flow(b, a, reach, prior)
    a1, b1 = halfway(a, b, fab)
    b2, a2 = halfway(b, a, fba)
    ok1, ok2 = same(a1, b1), same(a2, b2)
    # Per pixel, the direction whose neighbourhood agrees more.
    use1 = box(ok1.astype(np.float32), 2) >= box(ok2.astype(np.float32), 2)
    first = np.where(use1[..., None], a1, a2); second = np.where(use1[..., None], b1, b2)
    agree = np.where(use1, ok1, ok2)
    out = first.copy()
    # Disagreement: solid only if most of the neighbourhood's candidates are solid.
    cover = box(((first[..., 3] > 0).astype(np.float32) + (second[..., 3] > 0)) / 2, 1) / 9
    pick_second = ~agree & (first[..., 3] == 0) & (cover >= 0.5)
    out[pick_second] = second[pick_second]
    out[~agree & (cover < 0.5)] = 0
    out = despeckle(out)
    # Judged where the poses differ: the still parts of a body always agree.
    changed = (a != b).any(-1)
    return out, float(agree[changed].mean()) if changed.any() else 1.0


def similar(a, b):
    """Poses close enough to interpolate: their silhouettes mostly overlap and
    differ only by slivers. A part three pixels thick in one pose and absent
    from the other (an arm thrown out, a whip, in other words) has no counterpart
    to move to, and correspondence invents one."""
    sa, sb = a[..., 3] > 0, b[..., 3] > 0
    union = (sa | sb).sum()
    if not union: return False
    thick = box((sa ^ sb).astype(np.float32), 1) >= 9
    return (sa & sb).sum() / union >= 0.8 and thick.sum() / union <= 0.02


def on_canvas(image, anchor, size, origin):
    out = np.zeros((size[1], size[0], 4), np.uint8)
    x, y = origin[0] - anchor[0], origin[1] - anchor[1]
    out[y: y + image.shape[0], x: x + image.shape[1]] = image
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path); ap.add_argument('out', type=Path); ap.add_argument('dirs', nargs='+', type=Path)
    ap.add_argument('--sheet', type=Path); ap.add_argument('--only', help='one player, or "others"')
    ap.add_argument('--reach', type=int, default=10); ap.add_argument('--prior', type=float, default=0.004)
    ap.add_argument('--min-agreement', type=float, default=0.75)
    a = ap.parse_args()
    rom = a.rom.read_bytes()

    # Every extracted frame, by mapping: {colour key: (entry, directory)}; the most seen wins.
    frames = {}
    for d in a.dirs:
        for f in json.loads((d / 'index.json').read_text())['frames']:
            if not f['width']: continue
            slot = frames.setdefault(int(f['mapping'], 16), {})
            rank = (2 if 'character' in f else 1, f['seen'])
            if f['colours'] not in slot or rank > slot[f['colours']][2]:
                slot[f['colours']] = (f, d, rank)

    sets = {}
    if a.only in (None, *PLAYER_SETS):
        sets.update({n: b for n, b in PLAYER_SETS.items() if a.only in (None, n)})
    if a.only in (None, 'others'):
        known = set(frames); players = set(PLAYER_SETS.values())
        for base in range(0x200, len(rom) - 2, 2):
            if base in players or word(rom, base) < 4: continue
            anims = animations(rom, base, known)
            if anims and sum(len(x) for x in anims) >= 2:
                sets['set-%06X' % base] = base

    pairs = {}                                   # (from, to) -> set name
    for name, base in sets.items():
        for frames_of in animations(rom, base) or []:
            for p, q in zip(frames_of, frames_of[1:]):
                # A pose and its mirror image are not neighbours in space.
                if (p < 0) != (q < 0) or abs(p) == abs(q): continue
                pairs.setdefault((abs(p), abs(q)), name)

    a.out.mkdir(parents=True, exist_ok=True)
    index, cells = [], {}
    for (p, q), name in sorted(pairs.items()):
        for colours in sorted(set(frames.get(p, {})) & set(frames.get(q, {}))):
            (fp, dp, _), (fq, dq, _) = frames[p][colours], frames[q][colours]
            if fp['mask'] != fq['mask']: continue
            ia = load_pam(dp / ('%s_c%s.pam' % (fp['mapping'], colours))); ib = load_pam(dq / ('%s_c%s.pam' % (fq['mapping'], colours)))
            left = max(fp['anchor'][0], fq['anchor'][0]) + a.reach; top = max(fp['anchor'][1], fq['anchor'][1]) + a.reach
            right = max(ia.shape[1] - fp['anchor'][0], ib.shape[1] - fq['anchor'][0]) + a.reach
            bottom = max(ia.shape[0] - fp['anchor'][1], ib.shape[0] - fq['anchor'][1]) + a.reach
            size, origin = (left + right, top + bottom), (left, top)
            ca, cb = on_canvas(ia, fp['anchor'], size, origin), on_canvas(ib, fq['anchor'], size, origin)
            if (ca == cb).all(): continue                       # the same picture twice: nothing to smooth
            tween, agreement = inbetween(ca, cb, a.reach, a.prior)
            kept = bool(similar(ca, cb) and agreement >= a.min_agreement and (tween[..., 3] > 0).any())
            # Rejected drafts are written too: a hand-made in-between needs the canvas.
            save_pam(a.out / ('%06X_%06X_c%s.pam' % (p, q, colours)), tween)
            entry = {'from': '%06X' % p, 'mapping': '%06X' % q, 'colours': colours, 'mask': fp['mask'], 'set': name,
                     'width': size[0], 'height': size[1], 'anchor': list(origin), 'agreement': round(agreement, 3), 'kept': kept,
                     'seen': min(fp['seen'], fq['seen']), 'rounds': fp.get('rounds', 0) | fq.get('rounds', 0),
                     'types': sorted(set(fp['types']) | set(fq['types']))}
            if 'character' in fp: entry['character'] = fp['character']
            index.append(entry)
            if a.sheet:
                gap = np.zeros((size[1], 2, 4), np.uint8)
                shown = tween.copy()
                if not kept: shown[:2] = (255, 0, 0, 255)                              # rejected: red bar
                cells.setdefault(name if name in PLAYER_SETS else 'others', []).append(np.concatenate([ca, gap, shown, gap, cb], 1))
    (a.out / 'index.json').write_text(json.dumps({'frames': index}, indent=0) + '\n')
    if a.sheet:
        a.sheet.mkdir(parents=True, exist_ok=True)
        for name, group in cells.items():
            group = group[:120]
            hh = max(c.shape[0] for c in group) + 4; ww = max(c.shape[1] for c in group) + 8; cols = 6
            sheet = np.full(((len(group) + cols - 1) // cols * hh, cols * ww, 4), (40, 44, 52, 255), np.uint8)
            for n, c in enumerate(group):
                region = sheet[n // cols * hh: n // cols * hh + c.shape[0], n % cols * ww: n % cols * ww + c.shape[1]]
                region[c[..., 3] > 0] = c[c[..., 3] > 0]
            Image.fromarray(sheet).convert('RGB').resize((sheet.shape[1] * 2, sheet.shape[0] * 2), Image.NEAREST).save(a.sheet / (name + '.png'))
    kept = sum(1 for i in index if i['kept'])
    print(json.dumps({'sets': len(sets), 'pairs': len(index), 'kept': kept, 'rejected': len(index) - kept,
                      'mean_agreement': round(float(np.mean([i['agreement'] for i in index])), 3) if index else None}))


if __name__ == '__main__':
    main()
