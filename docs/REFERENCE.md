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
| Movement | Four directions, diagonal, plane bounds, jump arcs | Walking increments/end position match; jump phase differs |
| Combat | Combo presses/holds, back attack, jump kick, all grabs/throws, police | Not compared |
| Enemy logic | Every family, damage, invulnerability, knockdown, recovery | Not compared |
| Campaign | Scroll triggers/waves, transitions, all bosses, all endings | Not compared |
| Two-player | Join, friendly fire, grabs/assists, lives/continues, scoring | Both players active in encounter probe; combat differs |
| Randomness/cadence | Same reset/input stream, seeds and per-tick actor state | Native repeatability verified; original parity incomplete |

Record verified, inferred and inaccurate behavior separately. Never substitute
host wall-clock sleep for a deterministic frame input script. Any test using cheats
must label the altered setup and is not evidence of ordinary progression.

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
bus faults, but P2 X differs by 10 at the end. This is not a gameplay parity pass.
