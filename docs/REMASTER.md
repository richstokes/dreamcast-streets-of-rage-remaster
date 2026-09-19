# Graphical remaster: enhanced rendering path

Status (2026-09-19): **the rendering path and asset plumbing work, with
placeholder art only.** Original and enhanced graphics are selectable on the same
simulation, objects can be drawn with replacement art at twice the original
resolution, and frames can be extracted, packed, previewed and measured. No
redrawn art exists yet; the placeholders are the original frames doubled, with a
cyan outline and a magenta anchor cross, and do not count towards the remaster.

## How objects become replacement art

The Genesis draws everything as hardware sprite pieces. SoR builds its sprite
table each update from object records: `emit_object_sprite_mapping` (`$AF46`)
resolves an object's animation frame to a mapping record in ROM, computes the
object's screen anchor (its feet) and emits one table record per visible piece.

- **Sprite probe** (`src/render/sprite_probe.*`, hooks inserted by
  `tools/sprite_probe_patches.py`): while the game builds its table, the port
  records, per object, the mapping address, mirroring, anchor, tile base and
  the range of table records it emitted. Recording changes no game state or
  emulated time: both power-on replays, the state-synced windows re-run and all
  GPU scene checks are unchanged.
- **Matching the displayed table**: the RAM table reaches VRAM by DMA at the
  next graphics VBlank, so the probe keeps two builds and uses the one whose
  records are byte-for-byte those in VRAM. If neither matches, the frame falls
  back to the original pieces.
- **Frame key**: mapping address and palette line. A mirrored mapping uses the
  same art flipped about the anchor; enemy palette swaps are separate keys.
- **Enhanced scene** (`VdpScene::enhanced`): every sprite piece becomes 8x8
  cells drawn as their own quads, and an object with art becomes one quad in its
  first record's slot. Depth is the piece's priority layer (3 or 6, as in the
  original renderer) plus its link order, so replacement art keeps the original
  front-to-back order among sprites and priority against the planes. The
  original sprite layers are still composited, because they also produce the
  VDP's sprite overflow and collision status bits the game can read; they are
  not uploaded. VDP per-line sprite limits do not apply to enhanced drawing (no
  sprite dropout).
- **Art catalog** (`src/render/art_catalog.*`, package format in the header):
  pages of ARGB1555 texels plus frames `{mapping, palette, page, rect, anchor}`,
  art pixels at 2x. On the Dreamcast the pages go to PowerVR memory at start.

## Using it

```sh
# 1. Extract every object frame drawn in a replay (host build).
SOR_EXTRACT_FRAMES=$PWD/build/frames-round1-full build/headless/sor-headless "$SOR_ROM" REPLAY.bin build/r.ram
# 2. Placeholder package (derived from the ROM: stays in build/) and budget report.
build/tools-venv/bin/python3 tools/make-placeholder-art.py build/frames-round1-full --out build/art/SORART.PAK
# 3. Side-by-side previews: original at 2x (left), enhanced (right), plus a list of art drawn.
SOR_ART=$PWD/build/art/SORART.PAK SOR_ENHANCED_CAPTURE=$PWD/build/cap:1500:2700:100 \
  build/headless/sor-headless "$SOR_ROM" REPLAY.bin build/r.ram
# 4. Dreamcast: package.sh puts build/art/SORART.PAK (or $SOR_ART) on the disc;
#    SOR_ENHANCED=1 starts in enhanced graphics.
FLYCAST_VSYNC=0 SOR_ENHANCED=1 tools/bench-flycast.sh enh-actions
```

In game, L + R opens the options menu; GRAPHICS switches ORIGINAL / ENHANCED at
any time. Without an art package, enhanced mode draws the original sprites as
cells (identical pictures, no line limits).

## Measurements (Flycast, placeholder set, 2026-09-19)

| | Original | Enhanced |
| --- | --- | --- |
| Action replay: flips / VBlanks | 1,611 / 1,611 | 1,611 / 1,611 |
| Two-player: flips / VBlanks | 893 / 893 | 893 / 893 |
| Audio underruns / ring minimum (two-player) | 0 / 1,056 | 0 / 1,165 |
| GPU upload / commands per frame (two-player mean) | 467 / 198 µs | 370 / 235 µs |

Placeholder set: 149 frames from the Round 1 replays (Adam 47, enemy families
7-16 each, items and effects), 1.33 M texels:

| Format | Bytes | Notes |
| --- | --- | --- |
| ARGB1555, 6 pages of 512x512 | 3,145,728 | what the prototype loads; leaves 952 KB of PowerVR memory |
| ARGB1555, tight | 2,651,840 | |
| 8-bit palette | 1,325,920 | 256 colours per texture, 3 banks free beside the tile palettes |
| 4-bit palette | 662,960 | 16 colours per texture |
| VQ (estimate) | 343,768 | 2x2 blocks, 2 KB codebook per texture |

Loading the 3 MB package from the emulated CD takes 25.7 s at boot (about
120 KB/s). A real art set therefore needs compression (VQ or palettes) and
per-stage loading, as the brief requires. Adam's 47 frames seen in Round 1 are
about half a full player set: a full character at 2x is roughly 2 MB in
ARGB1555, 1 MB with an 8-bit palette, 0.25 MB with VQ.

## Known limits of the prototype

- Palette effects (screen fades, hit flashes, the police special's flash) do
  not reach replacement art, which has its own colours. The PowerVR can darken
  or tint a quad through its vertex colour; that mapping is still to do.
- Pieces the game culls at the screen edge are shown by replacement art (the
  whole frame is drawn and clipped by the screen).
- Placeholder frames come from what the replays showed; frames never drawn in
  a replay have no placeholder and fall back to the original pieces.
- Everything is uncompressed ARGB1555 and loaded at boot.
- Output is 640x480 from 320x224, so art is scaled 1:1 horizontally and by
  480/448 vertically, like the planes.
