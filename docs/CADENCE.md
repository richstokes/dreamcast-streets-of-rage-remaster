# Simulation and interrupt cadence

The native port runs translated 68000 code without an interpreter, so it has no
CPU clock of its own. VBlanks, the VDP's raster counters, DMA completion and the
placement of sound writes within a frame all depend on how much emulated CPU
time the code consumes. This document describes the model and how it is checked
against the original ROM running in Genesis Plus GX. Status: **established and
verified to about one frame per screen load; not frame-exact** (see Limits).

## Model

- **Emulated time** is kept in master clocks (`cycles_`; 7 per CPU cycle,
  896,040 per NTSC frame). VBlank begins at raster line 224, so the VDP model's
  counters agree with emulated time.
- **Translated instructions** charge their MC68000 time at staging
  (`BEFORE_INSTRUCTION_CYCLES(n)`, `tools/prepare-native.py`). The opcode is read
  from the ROM at the instruction's address and costed with the Musashi cycle
  table from the Genesis Plus GX research checkout (`tools/m68k-cycle-table.c`),
  plus MOVEM register counts and immediate shift counts (`tools/m68k_cycles.py`).
  Branches are charged as taken; register shift counts and MUL/DIV use typical
  values. DRAM refresh adds 2 cycles every 128, as in Genesis Plus GX.
- **VBlank**: crossing the next frame boundary while code runs is a VBlank (the
  handler runs when the interrupt mask allows; a masked VBlank stays pending).
  An explicit wait idles until the boundary. Both use one frame-boundary routine.
- **DMA**: 68K-to-VDP DMA halts the CPU for its duration. Transfer rates use
  Genesis Plus GX's per-line counts, with blanking rates when the display is
  disabled or the raster is in vertical blank; VRAM counts are words of two
  bytes (`tools/vdp_patches.py`).
- **Hand-written decompressors** (Nemesis, Kosinski, Enigma) count the events
  that dominate the original routines (codes, nibbles, literals, copies, bit
  refills) and charge a fitted cost after their writes, in chunks that let
  VBlanks and the handler run (`tools/game_patches.py`).

## Decoder costs

`tools/m68k-time.cpp` runs the ROM's own decompression routines on the Musashi
core with the game's register inputs and counts cycles. `tools/fit-decoder-cycles.py`
times ~50–400 valid streams per format found in the ROM, fits per-event costs,
and tests them on the game's own decode calls (`reference/results/decoder-cycles-2026-09-19.txt`):

| Format | Training inputs | Held-out game calls | Worst error |
| --- | ---: | ---: | ---: |
| Nemesis | 56 | 12 | 4,242 cycles (0.03 frame) |
| Kosinski | 53 | 17 | 4,706 cycles (0.04 frame) |
| Enigma | 407 (includes game calls) | — | 13,873 cycles (0.11 frame) |

## Verification against the original

`reference/results/cadence-2026-09-19.json`; `tools/compare-timeline.py` lists
frames per game mode, `tools/compare-phase.py` compares gameplay after the gate.

| | Before | Now |
| --- | --- | --- |
| Frames per mode vs original (action replay) | up to −38 (loads 7 vs 55 frames) | within ±1 in every mode |
| Replay gate frame (original 1,384 / 1,398) | 1,385 / 1,399 | 1,385 / 1,399 |
| Gameplay lag frames | — | none |
| Upload-VBlank counter `$FFFB08` equal until | frame 80 | frame 325 |
| Action replay observations (1,481) | equal | equal |
| Two-player observations (761) | equal | equal for 220 frames, then differ |

Stage music now starts within a frame of the original relative to gameplay (it
started 47 frames early), and in Flycast the stream no longer underruns during
screen loads (0 underruns in both replays, was 2).

## Limits

- Each long load still ends up to about a frame early or late (±3%): taken vs
  not-taken branches, register shift counts, bus wait states and the uncharged
  per-frame incremental art decode are approximations.
- SoR counts VBlanks that upload graphics (`$FFFB08`, VInt `$1A078`) and some
  objects copy it (for example type `$22` at `+$6C`), so a one-frame load
  difference can change behaviour much later. The earlier two-player match was
  coincidental: the old model was 24 counts off (a multiple of 8) and the value
  reached no observed field in the window. With the counter one behind, the
  type-`$22` object's fall diverges 221 frames after the gate.
- The hand-written object pass and menus cost no emulated time; gameplay frames
  are therefore never over-budget in the model, and the original's slowdown
  (if any) is not reproduced.

Frame-exact cadence would need cycle-exact emulation of the loads. Until then,
comparisons past the first counter-dependent behaviour need either identical
counters at the gate or tolerance for counter-seeded values.
