# Decision 001 — static gameplay, native Dreamcast platform

2026-09-15. Provisional until the ROM permits code generation and measurements.

Keep the statically translated 68000 gameplay and explicit register/CCR model
initially. Do not put a generic 68000 interpreter in the shipping game. Replace
MegaDriveEnvironment's desktop threading, memory and SDL boundary with KOS services.
Retain only the VDP/register semantics and sound emulation actually exercised by
SoR1 while progressively replacing them with semantic render/audio commands.

The first target is SoR1 JUE revision 00, overseas NTSC: 512 KiB raw ROM,
product `MK 00001019-00`, header checksum `9409`. This matches the supplied
SoR1 disassembly header, but is **not yet a verified identity for the recompiler's
address database**. Lock the supplied ROM SHA-256 after checking entry points and
comparison against a Genesis emulator. Header/checksum matching alone is insufficient.

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
The native memory implementation is not yet connected to generated gameplay.

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
| VRAM: framebuffers + PVR lists | 2 MiB |
| VRAM: stage textures | 5 MiB |
| VRAM: reserve | 1 MiB |
| AICA: driver/stream buffers | 512 KiB |
| AICA: effects | 1 MiB |
| AICA: reserve | 512 KiB |

Measure actual PVR initialization consumption: revise texture allowance downward
if framebuffers/lists exceed reservation. A 64x96 RGBA4444 sprite is 12 KiB before
power-of-two atlas packing; 128x192 is 48 KiB. Complete sets must be packed and
streamed by stage; keyframes alone do not satisfy animation coverage.
