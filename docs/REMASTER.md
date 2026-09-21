# Graphical remaster: enhanced graphics

Status (2026-09-21): original and enhanced graphics are selectable on the same
simulation. Enhanced mode draws the three players (every animation frame) and
the enemies, bosses, items and effects of all eight rounds with generated 2x
art. The art comes from an offline pipeline that redraws the original frames;
**it is not hand-drawn**, and any frame can be overridden with hand-made art.
Backgrounds, the HUD and text are unchanged.

Read this before touching `src/render/art_catalog.*`, `sprite_probe.*`,
`VdpScene::enhancedSprites`, `src/headless/extract_frames.cpp`, the loader in
`src/dreamcast/renderer_kos.cpp` or the art tools. Everything derived from the
ROM (frames, sheets, packages) stays under `build/`, out of git.

## Quick start

```sh
tools/build-headless.sh            # host build: extraction and previews
tools/build-profile-core.sh        # reference core, for the bot runs
build/tools-venv/bin/python3 -m pip install numpy pillow
tools/make-art-set.sh              # everything below; about 2 minutes -> build/art/SORART.PAK
./build-and-run.sh                 # test ELF with the package embedded, enhanced on, Flycast muted
```

Review the result in `build/art/sheets/<character or type-XX>.png` (original
pixel-doubled above, generated below), `build/art/frames/*.png` (every frame)
and `build/art/SORART.json` (frame counts, palette error, PowerVR bytes per
round). L + R in game opens the options menu; GRAPHICS switches modes.

Host preview of a replay, original left and enhanced right, with a text file
per frame listing the art drawn:

```sh
SOR_ART=$PWD/build/art/SORART.PAK SOR_ENHANCED_CAPTURE=$PWD/build/cap:1500:2700:100 \
  build/headless/sor-headless "$SOR_ROM" REPLAY.bin /dev/null
```

## How an object becomes replacement art at run time

The Genesis draws everything as hardware sprite pieces. SoR builds its sprite
table each update from object records: `emit_object_sprite_mapping` (`$AF46`)
resolves an object's animation frame to a frame record in ROM, computes the
object's screen anchor (its feet) and emits one table record per visible piece.

- **Sprite probe** (`src/render/sprite_probe.*`, hooks inserted into the
  translated code by `tools/sprite_probe_patches.py`): while the game builds
  its table the port records, per object, the frame record's address
  ("mapping"), mirroring, anchor, tile base (`+$0E`), animation set (`+$04`)
  and the range of table records it emitted. Recording changes no game state
  or emulated time.
- **Matching the displayed table**: the RAM table reaches VRAM by DMA at the
  next graphics VBlank, so the probe keeps two builds and uses the one whose
  records are byte-for-byte those in VRAM; if neither matches, the frame is
  drawn from the original pieces.
- **Art key** (`art_catalog.hpp`): the mapping address plus `colour_key()` of
  the sprite's CRAM line *over the entries the object's art uses* (each frame
  carries that mask). Why not just the palette line: enemy families share
  frames and a line while the game loads different colours (green, purple and
  yellow Signals), and replacement art has its colours baked in. Why not the
  whole line: lines also hold colours the object never uses, which stages
  cycle; keyed on those, one enemy looked like 40 different ones. Consequence:
  during fades and hit flashes the colours match no art and the object is
  drawn from its original pieces in the game's colours, which is the correct
  picture. `tools/art_key.py` computes the same key.
- **Enhanced scene** (`VdpScene::enhancedSprites`): an object with art becomes
  one quad in its first record's slot; every other sprite piece becomes 8x8
  cells drawn as their own quads. Depth is the piece's priority layer (3 or 6,
  as in the original renderer) plus its link order, so art keeps the original
  order among sprites and against the planes. Mirrored frames flip the art
  about the anchor. The software sprite layers are not built in enhanced mode:
  their other product, the VDP's sprite overflow/collision status bits, is
  never used by the game (the VBlank handler discards the status read, and
  replays with both bits forced clear keep every frame's RAM equal). Per-line
  sprite limits therefore do not apply (no dropout).
- **What is loaded** (`load_selection` in `renderer_kos.cpp`): all the art does
  not fit in PowerVR memory (about 4.1 MB free, 4.6 MB while enhanced mode
  frees the two software sprite-layer textures). Each page names its rounds
  and, for player art, its character. Every frame the runtime reports the
  round (`$FFFF02`) and the characters in play (players' animation sets) via
  `platform_game_state`; when they or the graphics mode change, pages are
  freed and loaded (0.5-1 s, at round changes and when a player joins). Pages
  are stored most important first; if memory runs out the rest are marked
  unloaded and their frames fall back to the original pieces. Main RAM is the
  other constraint (the game leaves 1-2 MB of heap): the package stays
  zlib-compressed in RAM (1.3 MB) and one page is inflated at a time.

Package format `SORART04`: see the header comment of `art_catalog.hpp` (three
255-colour palettes in PowerVR palette banks 1-3; bank 0 holds the tile
palettes; 8-bit pages 512 wide, 64-512 high).

## The art pipeline (`tools/make-art-set.sh`)

1. **Player runs** (reference core + `bot-play.py`, then replayed natively):
   Adam, Axel (RIGHT on the select screen) and Blaze (LEFT) through the
   scripted Round 1, action and combat scenarios and two bot runs. They supply
   the players' CRAM line and check step 3.
2. **Round sweeps** (native headless, `SOR_CHEATS=ROUND`: start at that round
   with infinite health, lives and specials): an open-loop script walks,
   punches in both lanes and calls the police every sixth cycle; Round 8 runs
   right to left, so its script is mirrored. `SOR_EXTRACT_FRAMES=dir` saves
   every object frame drawn, and, because everything but the players keeps its
   art resident in VRAM, **renders the whole animation set** of each object
   that has been on screen for 45 frames. `index.json` records for each frame
   the mask, CRAM line, rounds seen, object types, and how the set rendering
   compared with direct captures (`check`: confirmed / contradicted /
   set_only / direct / direct_culled). Contradictions are a handful per run
   (effects whose tiles are rewritten); confirmed frames are in the hundreds.
3. **`tools/extract-player-frames.py`**: player art is not resident (each frame
   record names two ROM-to-VRAM DMA records), so all 205 player frames come
   straight from the ROM: animation sets `$53EFE` Adam, `$49AE0` Axel, `$5E90A`
   Blaze; frame record = piece count, 2+2 box ids, upper and lower art ids
   (tables `$1A160` -> VRAM `$B000`, `$1A53E` -> `$B400`), 5-byte pieces. Every
   frame the replays also drew matches pixel for pixel.
4. **`tools/make-enhanced-art.py`**:
   - drops looks that are fades or flashes (every colour darker or lighter
     than a longer-seen look of the same frame) or were barely seen;
   - redraws each frame: Scale2x twice then averaged back to 2x (anti-aliased
     interior edges, rounded silhouette, 1-bit alpha for PowerVR punch-through),
     then an edge-preserving (bilateral) filter that turns the 16-colour
     originals' stepped ramps and dithering into continuous shading while
     outlines stay crisp;
   - quantises three palettes (players; objects seen in every round; the rest)
     with farthest-point seeding and k-means, no dithering;
   - crops, packs pages per (rounds, palette, character) group, orders them by
     importance and writes `SORART04`.
   `--style placeholder` (or `ART_STYLE=placeholder`) makes pixel-doubled
   frames with a cyan outline and a magenta anchor cross instead, to check
   alignment and coverage. `--override DIR` (or `ART_OVERRIDE=dir`) takes
   hand-made `<MAPPING>_c<KEY>.png` frames (RGBA, twice the extracted frame's
   size, same anchor; names as in `build/art/frames/`).

## Current set (2026-09-21)

940 frames in 67 looks (205 player frames; 379 transient looks dropped), 59
pages, 1,258,670 package bytes. Mean palette error 3.7 of 255 per channel.
PowerVR bytes with one player: 2.6-4.0 MB depending on the round (one
character is 0.75 MB); two different characters in rounds 5 and 6 exceed the
budget by a few hundred KB, and the least-seen pages fall back.

Flycast, two-player bot replay (8,402 frames), enhanced: 7,143 VBlanks / 7,133
flips, as before the enemy art; audio checksums unchanged. The disc build
loaded Round 1 plus two characters into 4,456,448 bytes with 165,704 free.

Coverage gaps: Mr. X and whatever else the Round 8 sweep does not reach
(it gets as far as the returning bosses); weapons in a player's hands are
separate objects and are covered; the character-select portraits, the HUD and
cutscene objects are left as they are (HUD types are excluded by
`WORLD_TYPES`).

## Known limits

- Generated art cannot add detail the 16-colour originals never had; thin
  details (hair strands, faces) soften. Hand-made overrides are the way to
  real redrawn art.
- Art pops in after a fade or flash ends, because those frames are drawn from
  the original pieces (see the art key). Tinting art through the PowerVR
  vertex colour would make fades seamless; not done.
- Pieces the game culls at the screen edge are shown by replacement art (the
  whole frame is drawn and clipped by the screen).
- A few set renderings carry stray pixels where an effect's tiles had been
  rewritten when the set was sampled.
- Round changes and a second player joining cost a 0.5-1 s load.
- Output is 640x480 from 320x224: art is 1:1 horizontally and scaled 480/448
  vertically, like the planes. A two-texel gutter around packed frames stops
  the PowerVR sampling neighbours.

## History of decisions (why things are the way they are)

- `SORART01` ARGB1555 pages (3 MB for 149 frames, 25 s to read from CD) ->
  `02` 8-bit palette -> `03` zlib pages, because a 3.4 MB package embedded in
  the test ELF left the game no heap (`std::bad_alloc`) and 5.2 MB overran
  16 MB (Flycast: "Invalid load address", boot loop) -> `04` colour keys with
  masks, rounds, characters.
- The Dreamcast B button used to toggle a software comparison renderer (85 ms
  a frame, original graphics) in every build; it was pressed by accident and
  reported as "art resets and the frame rate collapses". It now needs
  `SOR_SOFTWARE_TOGGLE=1`. For any report of slowness read the run's log
  first (`build/logs/flycast.log`: `Renderer:`, `SLOW`, `HEARTBEAT`,
  `Enhanced art:` lines; `tools/flycast-speed.py` follows it live).
