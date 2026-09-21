# Decision 001 — static gameplay, native Dreamcast platform

2026-09-15. Implemented platform separation; full gameplay parity remains pending.

Keep the statically translated 68000 gameplay and explicit register/CCR model
initially. Do not put a generic 68000 interpreter in the shipping game. Replace
MegaDriveEnvironment's desktop threading, memory and SDL boundary with KOS services.
Retain only the VDP/register semantics and sound emulation actually exercised by
SoR1 while progressively replacing them with semantic render/audio commands.

The first target is SoR1 JUE revision 00, overseas NTSC: 512 KiB raw ROM,
product `MK 00001019-00`, header checksum `9409`. This matches the supplied
SoR1 disassembly header. The supplied ROM is locked by SHA-256 in REFERENCE.md,
and the recompiler entry list has been repaired and exercised against that image.
Header/checksum matching alone is insufficient.

## Boundaries

- Simulation: single owner of 64 KiB big-endian WRAM, fixed-width registers,
  CCR flags, original random seed/state, original VBlank/mailbox ordering.
  KOS scheduling, input polling and wall time never enter combat calculations.
- Input: two controller ports sampled/latching at simulation boundaries. Raw
  Genesis button mask and press edges go into recorded frame inputs.
- Render: immutable frame description + stable art/frame IDs, 320x224 gameplay
  coordinates mapped to 4:3 640x480 output. Original and enhanced mode consume
  the same snapshot. Pivots and presentation interpolation cannot change hitboxes.
- Audio: ordered original sound commands and deterministic chip clocks; initially
  measure retained YM/PSG cost. KOS AICA output/streaming replaces SDL. A low-cost
  offline-rendered original-music stream is a candidate, not an implemented substitute
  for validating dynamic sound effects or synchronization.
- Storage: versioned explicit-byte saves in KOS VMU packages. Alternate two files
  so a failed overwrite leaves a previous valid copy. Missing/full/corrupt VMU
  leaves defaults or the last valid record. Never write during active combat.
- Assets: `/cd` stage packages, bounded staging buffers, stage-specific texture
  residency. Host paths exist only in tools. No dependency on `/sd`.

Native C11 modules implement bounded I/O and services; C++ hosts generated
simulation/CPU helpers. No SH-4 assembly until profiling justifies it. No float
replacement for gameplay fixed-point or signed/unsigned arithmetic.

## Migration gates

1. ROM identity; build upstream; deterministic original-emulator reference.
2. Generated code compiles on SH-4; code size and stack/heap fit; one section plays
   with original sprites, collision, timing, sound and both players.
3. Compare that section before redrawing complete character animation sets.
4. Extend parity through all eight rounds, bosses, endings and two-player branches.
5. Complete enhanced art, full playthrough, performance budgets and hardware checklist.

Current implementation is a platform checkpoint. Steps 1–5 are not complete.
Generated gameplay now uses the native memory implementation. The first section
runs with both players, but timing parity, sound and complete campaign coverage
remain incomplete.

## Initial budgets (reservations, NOT measured game peaks)

| Pool | Budget |
| --- | ---: |
| Main: code + read-only data | 4 MiB |
| Main: KOS/runtime + stacks | 2 MiB |
| Main: simulation + ROM + device state | 1 MiB |
| Main: active asset staging/cache | 4 MiB |
| Main: audio buffers | 1 MiB |
| Main: I/O buffers | 512 KiB |
| Main: reserve/headroom | 3.5 MiB |
| VRAM: framebuffers + PVR lists | 3.5 MiB |
| VRAM: stage textures | 3.5 MiB |
| VRAM: reserve | 1 MiB |
| AICA: driver/stream buffers | 512 KiB |
| AICA: effects | 1 MiB |
| AICA: reserve | 512 KiB |

Measure actual PVR initialization consumption: revise texture allowance downward
if framebuffers/lists exceed reservation. A 64x96 RGBA4444 sprite is 12 KiB before
power-of-two atlas packing; 128x192 is 48 KiB. Complete sets must be packed and
streamed by stage; keyframes alone do not satisfy animation coverage.

## PowerVR migration checkpoint

Gameplay still writes the SoR VDP register/VRAM model. The renderer translates a
frame snapshot into clipped tile quads rather than rasterizing both background
planes pixel by pixel on SH-4. Four palette variants of each referenced 8x8 tile
are cached as ARGB1555 PowerVR textures; only changed tile/palette data is uploaded.
Transparent tiles are omitted. Background geometry and complete static scenes are
cached independently from sprite animation. DMA bookkeeping registers do not
invalidate geometry. Priority uses six depth levels, including separate window
and low/high sprite composition.

The native sprite evaluator traverses the linked SAT once per frame, retaining
per-scanline order, masking, hardware limits, and collision/overflow flags. It
emits two transparent sprite layers for PowerVR composition. Only the union of
previous and current occupied row extents is cleared/uploaded, so moved or hidden
sprites cannot leave stale pixels. Tests compare against the upstream scanline
renderer, including crowded lines, invalid links and tile spans beyond VRAM.
This is still an intermediate renderer; sprite pixels are composed on the CPU. Shadow/highlight,
interlace, two-cell vertical scrolling, or more than 6,000 tile quads use the
software fallback. These cases need further native work before full-game
performance claims. In builds made with `SOR_SOFTWARE_TOGGLE=1`, Dreamcast B toggles the fallback for visual comparison (it is about 85 ms a frame, so it is off by default).

Actual allocations: 1 MiB tile VRAM, 512 KiB sprite VRAM, 256 KiB software fallback
VRAM, plus KOS frame/list buffers. Observed free VRAM is 3,136,104 bytes. CPU scene
storage is approximately 1.1 MiB, plus 256 KiB headers and a 64 KiB tile snapshot;
a further 960,320 bytes caches aligned header/vertex packets. Unchanged geometry
is sent with a single KOS store-queue submission. Packet invalidation includes
plane geometry and transparent/visible changes of tiles actually used by planes;
sprite-only pattern changes do not rebuild the background packets. These buffers
are part of measured heap/BSS, not extra pools to add twice.
RGB1555 has 5-bit color channels; tests compare the original output in that format.
This renderer is original presentation, not remastered artwork.
