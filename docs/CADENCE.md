# Simulation and interrupt cadence

The native port runs translated 68000 code without an interpreter, so it has no
CPU clock of its own. VBlanks, the VDP's raster counters, DMA completion and the
placement of sound writes within a frame all depend on how much emulated CPU
time the code consumes. This document describes the model and how it is checked
against the original ROM running in Genesis Plus GX. Status: **established and
verified to about one frame per screen load; gameplay slowdown reproduced to within
0.13% of a frame in Round 1; not frame-exact** (see Limits).

## Model

- **Emulated time** is kept in master clocks (`cycles_`; 7 per CPU cycle,
  896,040 per NTSC frame). VBlank begins at raster line 224, so the VDP model's
  counters agree with emulated time.
- **Translated instructions** charge their MC68000 time at staging
  (`BEFORE_INSTRUCTION_CYCLES(n)`, `tools/prepare-native.py`). The opcode is read
  from the ROM at the instruction's address and costed with the Musashi cycle
  table from the Genesis Plus GX research checkout (`tools/m68k-cycle-table.c`),
  plus MOVEM register counts and immediate shift counts (`tools/m68k_cycles.py`).
  Conditional branches charge the path taken (Bcc.s 8/10, Bcc.w 12/10, DBcc
  12/10/14). MULU/MULS charge 38 + 2n from the multiplier as executed (set bits,
  or 01/10 pairs for MULS). Register shift counts and DIV use typical values.
  DRAM refresh adds 2 cycles every 128, and each access to the Z80 area
  (`$A00000-$A0FFFF`) one cycle, as in Genesis Plus GX.
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
  VBlanks and the handler run (`tools/game_patches.py`). The incremental queue
  (`$8510`) decodes and charges each tile when it is uploaded, five per VBlank.
- **Other hand-written routines** charge their ROM cost: the object pass per
  slot (40 empty, 154 active) and per update, the joypad sampler, the pickup
  scan (`$3136`, per slot scanned), the main loop, and mean per-call costs for
  input remapping, attack descriptors, attack input and the sound queue.
- **Sound bus**: `sound_ym2612_acquire` charges the ROM loop per path. The YM2612
  busy flag lasts 32 YM clocks after a data write. The Z80 DAC driver's busy
  flag (`$A01FFD` bit 7) comes from a shadow of the native driver that advances
  with 68000 time from the frame's start and stalls while the 68000 holds the
  bus, so the acquire retries while a drum sample is being written, as on
  hardware (up to ~5k cycles per frame).

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

## Gameplay slowdown (Round 1)

SoR updates every two VBlanks (mailbox `$FFFA00`); when an update runs past its
second VBlank the game slows for a frame. `reference/scenarios/round1-full.json`
plays 26,415 gameplay frames of Round 1 after the gate. The per-routine cycle
histograms of both backends (`tools/gpgx-profile-report.py`, see REFERENCE.md)
located the missing time: sound-bus DAC-busy retries (~5k cycles in busy
frames), branch and multiply timing, the uncharged object pass and pickup scan
(3.4k cycles in one frame), and Z80-area wait states.

| | Start of this work | Now |
| --- | --- | --- |
| Observations equal until (relative frame) | 8,262 | 8,283 |
| First two slowdowns (original 8261, 8272) | not reproduced | same updates |
| Idle time per frame near the slowdowns | — | within ~1.5k cycles (1%) |
| Third slowdown (original 8283) | not reproduced | missed by 163 cycles (0.13%) |

Round 1 combat (`round1-combat.json`, 3,115 frames) and the action replay
(1,481 frames) match in every observed field. Results:
`reference/results/behaviour-round1-2026-09-19.json`.

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
- Gameplay slowdown depends on the frame's total within about 0.1%; the model
  is closer than that on average but not per frame. Remaining known
  approximations: DIV and register-count shifts use typical times, the Nemesis,
  Kosinski and Enigma costs are fitted (±0.1 frame per decode), hand-written
  routines charge mean costs, a DAC sample that starts during a frame is not
  seen until the next (the shadow driver starts at frame boundaries), and 68000
  bus accesses happen at the end of each charged instruction rather than
  mid-instruction.

Frame-exact cadence would need cycle-exact emulation of the loads. Until then,
comparisons past the first counter-dependent behaviour need either identical
counters at the gate or tolerance for counter-seeded values.
