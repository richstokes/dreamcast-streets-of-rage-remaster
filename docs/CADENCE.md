# Simulation and interrupt cadence

The native port runs translated 68000 code without an interpreter, so it has no
CPU clock of its own. VBlanks, the VDP's raster counters, DMA completion and the
placement of sound writes within a frame all depend on how much emulated CPU
time the code consumes. This document describes the model and how it is checked
against the original ROM running in Genesis Plus GX. Status: **frame-exact from
power-on through the menus and every screen load of the checked replays; Round 1
play matches for 10,045 gameplay frames, where one slowdown frame is missed**
(see Limits).

## Model

- **Emulated time** is kept in master clocks (`cycles_`; 7 per CPU cycle,
  896,040 per NTSC frame). VBlank begins at raster line 224, so the VDP model's
  counters agree with emulated time. At power-on the 68000 starts at line 191,
  111,856 clocks before the first VBlank (Genesis Plus GX, from the VDP's fixed
  power-on position).
- **Translated instructions** charge their MC68000 time at staging
  (`BEFORE_INSTRUCTION_AT(n, pc)`, `tools/prepare-native.py`). The opcode is read
  from the ROM at the instruction's address and costed with the Musashi cycle
  table from the Genesis Plus GX research checkout (`tools/m68k-cycle-table.c`),
  plus MOVEM register counts and immediate shift counts (`tools/m68k_cycles.py`).
  Conditional branches charge the path taken (Bcc.s 8/10, Bcc.w 12/10, DBcc
  12/10/14). MULU/MULS charge 38 + 2n from the multiplier as executed (set bits,
  or 01/10 pairs for MULS). Register shift counts and DIV use typical values.
  DRAM refresh adds 2 cycles every 128, and each access to the Z80 area
  (`$A00000-$A0FFFF`) one cycle, as in Genesis Plus GX.
- **Interrupts**: each VBlank leaves VINT pending until acknowledged; it
  interrupts the 68000 (level 6) only while VDP register 1 enables it (IE0), 788
  clocks after the VBlank flag (770 in H32). Enabling IE0 with a word write while
  VINT is pending takes effect one instruction later, and the exception costs
  44 cycles. An explicit wait idles until the next VINT, or returns at once if
  one is already pending and unmasked (the game disables IE0 during some loads
  and the pending VINT is then taken as soon as it re-enables it).
- **DMA**: 68K-to-VDP DMA halts the CPU for its duration. Transfer rates use
  Genesis Plus GX's per-line counts, with blanking rates when the display is
  disabled or the raster is in vertical blank; VRAM counts are words of two
  bytes (`tools/vdp_patches.py`).
- **Hand-written decompressors** (Nemesis, Kosinski, Enigma, the incremental
  Nemesis queue and the Z80 driver loader) add the ROM routine's time path by
  path (`tools/decoder_patches.py`), then charge it in chunks that let VBlanks
  and the handler run. The incremental queue (`$8510`) decodes each tile when it
  is uploaded, five per VBlank, as the original does.
- **Other hand-written routines** charge their ROM cost: the wait routines, the
  sound-bus acquire loop, the pickup scan (`$3136`, per slot scanned), the main
  loop and `restore_player_continues`, from their disassembly; the object pass,
  joypad sampler, input remapping, attack descriptors, attack input and the
  sound queue from Genesis Plus GX profiles (mean per call). A decode the port
  adds (the top-10 re-seed) costs no time.
- **Sound bus**: the YM2612 busy flag lasts 32 YM clocks after a data write. The
  Z80 runs ahead to the 68000's time whenever the 68000 touches the Z80 bus, its
  RAM or the bus/reset lines (catch-up), and stays stopped while the 68000 holds
  the bus, so commands reach the driver on time and the acquire retries while a
  drum sample is being written, as on hardware (up to ~5k cycles per frame).

## Decoder time

The decompressors' time follows the ROM routine's own decisions, including its
bit-window refills and data-dependent shifts, costed from the disassembly with
the Musashi table:

| Routine | Time (CPU cycles) |
| --- | --- |
| Kosinski `$85A2` | 48 entry + 200 end; literal 66; short copy 186 + 32/byte; long 178 + 32/byte; extended 222 + 32/byte; marker 192; descriptor refill 44 |
| Nemesis `$8192`/`$81A4`/`$84BA`/`$8510` | code table per group, descriptor and definition; per code 76 + 2n (window shift) + refill; inline codes likewise; per nibble 34, row end 40 (+8 XOR), writer 42/48; per-call setup and queue bookkeeping |
| Enigma `$82D6`/`$82D2`/`$12832`/`$112C0` | 226 entry; per op 138 + 2n + refill, then 26/18/18/22/22/36 per word by op; inline words 8 + attribute bits (14/32/38) + index bits (three window cases); end with the ROM's A0 alignment |
| Z80 driver loader `$1061C` | Kosinski decode + 7,879 byte copies (13 with the Z80-bus wait, DBF 10/14) + setup |

`tools/test-decoder-cycles.py` runs a replay natively (`SOR_DECODE_LOG`) and
times every distinct decode, and each incremental stream as a whole, with
`tools/m68k-time` running the ROM routine. Every decode of the Round 1 and
two-player replays matches to the cycle (44 and 39 decodes;
`reference/results/decoder-cycles-exact-2026-09-19.txt`). DRAM refresh, which
the timing tool can exclude (`M68K_NO_REFRESH=1`), adds 2 cycles per ~130-134
in these loops when the time is charged. The fitted per-event costs used
before were off by up to 300,000 cycles for the Z80 driver's Kosinski stream.

## Verification against the original

`tools/compare-timeline.py` lists frames per game mode, `tools/compare-phase.py`
compares gameplay after the gate, and `tools/compare-calls.py` aligns the
routine-entry timelines of both backends (`genesis_reference.py --watch`,
`SOR_WATCH`) to show where emulated time drifts. Both harnesses number frames
from power-on (frame 1 ends at the first VBlank) and deliver replay input N at
VBlank N.

| | Before | Now |
| --- | --- | --- |
| Frames per mode vs original (two-player replay) | within ±2 | equal in every mode |
| Replay gate frame (original 1,384 / 1,398) | 1,385 / 1,399 | 1,384 / 1,398 |
| Action replay observations (1,481) | equal | equal, and all object bytes |
| Two-player observations (761) | equal for 220 frames | equal, and all object bytes |
| Round 1 combat (3,115) | equal | equal |
| Round 1 play (26,415) equal until | 8,283 | 10,045 |

## Gameplay slowdown (Round 1)

SoR updates every two VBlanks (mailbox `$FFFA00`); when an update runs past its
second VBlank the game slows for a frame. `reference/scenarios/round1-full.json`
plays 26,415 gameplay frames of Round 1 after the gate. The original slows at
relative frames 8,261, 8,272, 8,283, 9,976, 10,046, 10,173, 10,564, …; native
reproduces 8,261, 8,272, 8,283, 10,046, 10,173 and 10,564 on the same frames
and misses 9,976, after which object state differs (10,046).
`reference/results/behaviour-round1-2026-09-19.json`.

The per-routine cycle histograms of both backends (`tools/gpgx-profile-report.py`)
and the call timelines located the remaining differences in turn: sound-bus
DAC-busy retries, branch and multiply timing, uncharged hand-written routines,
Z80-area wait states, the decoders' fitted costs, VINT gating, latency and
delay, the power-on position, and a one-frame input offset in the native replay.

## Limits

- A gameplay update whose total is within a fraction of a percent of its budget
  can finish on the other side of a VBlank. Remaining approximations: DIV and
  register-count shifts use typical times; hand-written routines costed from
  profiles charge means; Z80 reads of the 68000 bus (drum samples) cost neither
  side the wait states Genesis Plus GX gives them; 68000 bus accesses happen at the end of each
  charged instruction rather than mid-instruction; interrupts are taken at the
  next charged instruction of translated code.
- SoR counts VBlanks that upload graphics (`$FFFB08`) and some objects copy it,
  so one missed or extra slowdown frame changes behaviour later. The Round 1
  comparison is therefore exact until the first slowdown the model misses.
