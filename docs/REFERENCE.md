# Reproducible reference

## ROM identity

User-supplied `original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md`:

- Size 524,288; raw big-endian, JUE / `MK 00001019-00`.
- Header and computed checksum `9409`.
- SHA-256 `dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0`.
- Executed as overseas NTSC in Genesis Plus GX and rebuilt PC reference.
- ROM and extracted data are ignored. Do not commit them.

## Upstream defect and reproducible repair

The pinned upstream generation script fails with `movea.b` at 014700. Its auxiliary
address list includes 0146FC, which is inside `ori.b #4,1(a1)` at 0146F8;
014700 is the displacement of the following JSR. The SoR1 disassembly and ROM
agree here. `tools/generate.py` reconstructs instruction boundaries by seeding
sub/loc/locret labels from the disassembly, removes 25 conflicting original seeds,
and rejects an additional undecodable seed at 014BDC. It asserts the expected
conflict set and exact ROM hash. It does not replace unsupported opcodes with no-ops.
Research source remains untouched; the repaired address list and generation report
are local build products.

Only labels within the affected Abadede range (0143D0–0158C3) are added to
generation seeds. All other disassembly labels establish instruction boundaries
only. Adding every valid label changes function partitioning: an initial attempt
broke the manual call to `enqueue_object_render_bucket(0xAE96u)`, leaving actors
invisible. Narrowing the repair restores that entry and visible actors in Flycast.

Result: 25,518 decoded instructions, 844 function partitions, 53 manual entries;
24,408 translated instructions, zero stubbed instructions, 30 translation units.
These are generation counts, **not runtime coverage or fidelity percentages**.

## Runs and limits

- Rebuilt PC executable reached title; captured using its binary remote API.
- Genesis Plus GX libretro built locally and ran original ROM. Headless harness
  supports two pads, per-frame normalized RAM, framebuffer capture and audio frame counts.
- Initial input timings landed in menus/round-intro rather than gameplay; adjusted
  the scenario after checking mode values and captures. Names alone do not prove coverage.
- Unmodified PC lockstep timed out around boot frame 110. Thread sampling found
  the CPU asleep on interrupt generation while the renderer was paused at the
  lockstep boundary. `patches/reference-lockstep.patch` wakes the CPU at that
  boundary and samples generation before the checkpoint to close the lost-wakeup
  window. Applied only to an ignored staged runtime, preserving research pins.
- Two patched PC runs completed all 1,556 frames. They differed in 270 actor
  snapshots and 1,509 RAM hashes among 1,555 common frames. The wakeup fix does
  not make this threaded runtime deterministic. Preserve that distinction.
- PC silent mode drops chip writes. Its captures cannot validate sound.

## Commands

```
python3 tools/bootstrap.py
./tools/build-reference.sh /absolute/path/to/user-ROM.md
./tools/run-reference.sh /absolute/path/to/user-ROM.md
python3 tools/pc_reference.py reference/scenarios/boot-movement.json build/pc-reference

make -C research/Genesis-Plus-GX -f Makefile.libretro platform=osx ARCH=arm64 -j6
build/tools-venv/bin/python3 tools/genesis_reference.py \
  research/Genesis-Plus-GX/genesis_plus_gx_libretro.dylib /absolute/path/to/user-ROM.md \
  reference/scenarios/boot-movement.json build/genesis-reference
```

The libretro harness currently assumes a little-endian core with word-swapped WRAM;
validate the memory byte order before using another host. Genesis Plus GX is a
comparison tool only, never linked into the Dreamcast executable.

## Coverage required before fidelity claims

| Area | Required scenarios | Current evidence |
| --- | --- | --- |
| Movement | Four directions, diagonal, plane bounds, jump arcs | Phase-anchored directions, diagonals and jump actions match all object state; Round 1 play matches for 9,976 frames; plane bounds not exhausted |
| Combat | Combo presses/holds, back attack, jump kick, all grabs/throws, police | Phase-anchored attack/jump/special inputs compared; all grabs/throws still missing |
| Enemy logic | Every family, damage, invulnerability, knockdown, recovery | Round 1 waves 0–1 (Garcia-family, Signal, Haku-Ro) match for 9,976 frames (`round1-full`, 2026-09-19); other families and bosses not compared |
| Campaign | Scroll triggers/waves, transitions, all bosses, all endings | Boot, menus and loads frame-exact; Round 1 to wave 2, a death and respawn, and the slowdown frames match until 9,976 (CADENCE.md; FIDELITY_GATE.md); later waves, boss and transitions not compared |
| Two-player | Join, friendly fire, grabs/assists, lives/continues, scoring | 761 encounter frames match including all object bytes (2026-09-19); broader interactions still required |
| Randomness/cadence | Same reset/input stream, seeds and per-tick actor state | Native repeatability verified; cadence frame-exact through boot and loads, gameplay slowdown near-exact (CADENCE.md) |

Record verified, inferred and inaccurate behavior separately. Never substitute
host wall-clock sleep for a deterministic frame input script. Any test using cheats
must label the altered setup and is not evidence of ordinary progression.

## Per-routine cycle profiles

To find where native emulated time differs from the original, profile the same
frames on both sides by routine (`labels.csv`):

```sh
tools/build-profile-core.sh   # Genesis Plus GX with a per-PC cycle hook (HOOK_CPU)
build/tools-venv/bin/python3 tools/genesis_reference.py \
  build/gpgx-profile/genesis_plus_gx_libretro.dylib "$SOR_ROM" \
  reference/scenarios/round1-full.json build/genesis-prof --profile 9656:9656:$PWD/build/g.bin
SOR_PC_HISTOGRAM_FRAMES=9656:9656:$PWD/build/n.bin \
  build/headless/sor-headless "$SOR_ROM" build/native-round1-full/replay.bin /tmp/r.bin
python3 tools/gpgx-profile-report.py build/g.bin build/n.bin --top 40
```

`--profile` may repeat for disjoint frame ranges. For where time drifts rather
than how much, record routine-entry timelines on both sides and align them:

```sh
build/tools-venv/bin/python3 tools/genesis_reference.py build/gpgx-profile/genesis_plus_gx_libretro.dylib \
  "$SOR_ROM" SCENARIO build/genesis-watch --watch pcs.txt:LAST:$PWD/build/watch-g.txt
SOR_WATCH=pcs.txt:LAST:$PWD/build/watch-n.txt build/headless/sor-headless "$SOR_ROM" REPLAY.bin /tmp/r.bin
python3 tools/compare-calls.py build/watch-g.txt build/watch-n.txt --frames
```

`pcs.txt` lists hex routine addresses (for example every label in `labels.csv`).
`tools/test-decoder-cycles.py ROM REPLAY.bin` checks the decompressors' time
against the ROM routines (`tools/m68k-time`). Native frame N and original
frame N are the same point in SoR's two-VBlank update cycle here (the replay
gates differ by one frame). The native histogram charges each translated
instruction and each modelled charge to its ROM address; DMA stalls are
reported separately, and DRAM refresh and Z80-area wait states are charged
but not histogrammed (the original's histogram includes them, about 1.5%).
`SOR_WAIT_LOG=FIRST:LAST` (headless) logs when each frame starts waiting.

## Shared native headless backend

```
./tools/build-headless.sh
python3 tools/native-reference.py /absolute/path/to/user-ROM.md \
  reference/scenarios/boot-movement.json build/native-a
python3 tools/native-reference.py /absolute/path/to/user-ROM.md \
  reference/scenarios/boot-movement.json build/native-b
python3 tools/compare-traces.py build/native-a/trace.jsonl \
  build/native-b/trace.jsonl --require-equal
python3 tools/test-gameplay.py build/genesis-reference/trace.jsonl \
  build/native-a/trace.jsonl build/native-b/trace.jsonl
```

The headless executable shares generated/manual gameplay, synchronous interrupts,
MMIO, renderer and binary input replay with the console. Only display/input/time
services differ. It creates no GUI. Host speed is never Dreamcast performance evidence.
Each record contains all 64 KiB of WRAM; frame 0 is the first pre-VBlank checkpoint.
The final PPM depicts the preceding presentation, explicitly separate from RAM phase.
These large local artifacts are ignored. Snapshots now cover all 66 enemy slots;
P2 lives is byte FFFF23, verified against the disassembly (earlier traces used FFFF40).

`two-player-combat-smoke.json` selects two-player mode with Down in the menu,
confirms both players, walks toward enemies and pulses attack. The earlier
`encounter-smoke.json` Start-join probe stays in one-player mode; its name/status
records that limitation. The two-player probe reaches 2,158 frames without native
bus faults. Its unaligned cold-boot comparison put P2 X 10 pixels apart; the
phase-anchored comparison below removes that discrepancy. This is not full parity.


## Explicit simulation-phase comparisons

The ROM posts byte 1 or 2 at WRAM `FA00` for its VBlank upload/no-upload waits
(`10502`, `10514`; handler `19D16`). At the old movement input boundary the
original is waiting with mailbox 2, while native is at mailbox 1. Both sample the
same held/pressed buttons, but they update objects on opposite frame parities.
Do not fix this by changing walking speed or jump arithmetic.

SRP2 adds a bounded, read-only byte predicate to input playback. A `wait` segment
releases both pads until `(RAM[address] & mask) == value`, then starts the next
segment immediately. `frames` is its maximum idle-frame budget, not a fixed delay.
Both harnesses record the exact gate frame. No game state is overwritten and no
comparison offset is searched after the run. SRP1 fixed-frame playback remains
supported; the loader allows at most 4,096 segments and rejects invalid predicates,
pressed buttons on gates, truncated records and trailing bytes. Timeout is a
reported failure. `tools/test-replay.sh` checks these boundaries under sanitizers.

```sh
./tools/build-headless.sh
build/tools-venv/bin/python3 tools/genesis_reference.py \
  research/Genesis-Plus-GX/genesis_plus_gx_libretro.dylib "$SOR_ROM" \
  reference/scenarios/phase-aligned-actions.json build/genesis-actions --raw-ram
SOR_VALIDATE_GPU_SCENE=1 python3 tools/native-reference.py "$SOR_ROM" \
  reference/scenarios/phase-aligned-actions.json build/native-actions
python3 tools/compare-phase.py build/genesis-actions build/native-actions \
  --segment 9 --require-observations-equal
```

Use `phase-aligned-two-player.json` and `--segment 11` for the encounter probe.
Comparison requires identical ROM/scenario hashes and reports both anchor frames,
all remaining-frame counts, selected observations, and active-object byte regions.
Both backends number frames from power-on (frame 1 ends at the first VBlank; native
captures are labelled to match) and deliver replay input N at VBlank N, so anchors
and frames compare directly.
`--require-observations-equal` does **not** require entire-object/WRAM equality.

Verified against original ROM execution:

- Directional/action scenario: **1,481** paired observations match mode, stage,
  wave, camera, lives and all observed actor positions/states/health. Active-object
  collision IDs, fixed-point positions/velocities, damage, input and attack flags
  also match. Police stock goes 1 to 0 and the control lock lasts **637** frames
  in both backends. The script waits for completion and moves right afterward.
- Two-player encounter: **761** paired observations matched those same public
  gameplay fields with the 2026-09-15 build. Collision IDs, fixed-point positions/
  velocities, damage, input, weapon/grab fields and attack flags matched in the
  sampled frames. This alone does not prove each grab or weapon action was exercised.
  With the frame-exact cadence (CADENCE.md, 2026-09-19) all 761 observations and
  all active-object bytes match; the gate is reached on the same frame (1,398).
- Directional/action replay repeats with **2,866 identical native RAM snapshots**;
  all **2,865** rendered frames match the original-mode software renderer in RGB1555.
- Existing SRP1 two-player replay retains all 2,159 prior native RAM snapshots.

- Round 1 (`round1-combat.json`, 244 segments): **3,115** paired observations and
  all active-object regions match. `round1-full.json` (1,869 segments, 26,415
  gameplay frames, `--segment 9`) matches until relative frame **9,976**, where
  the original's update ends 79 cycles before its VBlank and native's overruns (CADENCE.md; `reference/results/behaviour-round1-2026-09-19.json`).
- Every game mode from power-on to Round 1 lasts the same number of frames in
  both backends (`tools/compare-timeline.py`).

Remaining differences are reported, not filtered out of the raw data. The earlier
start-up differences in spawn/timer and animation bytes came from the native replay
reading each input one VBlank late and are gone. Complete combat/campaign parity
remains open. Older results in `reference/results/phase-*.json` remain historical
evidence.
