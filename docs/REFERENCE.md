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

Result: 25,959 decoded instructions, 1,273 function partitions, 53 manual entries;
25,560 translated instructions, zero stubbed instructions, 30 translation units.
These are generation counts, **not runtime coverage or fidelity percentages**.

## Runs and limits

- Rebuilt PC executable reached title; captured using its binary remote API.
- Genesis Plus GX libretro built locally and ran original ROM. Headless harness
  supports two pads, per-frame normalized RAM, framebuffer capture and audio frame counts.
- Initial input timings landed in menus/round-intro rather than gameplay; adjusted
  the scenario after checking mode values and captures. Names alone do not prove coverage.
- PC one-frame lockstep replay timed out around boot frame 110; a whole-segment
  attempt also timed out. This prevents declaring deterministic parity. Keep
  failed logs distinct; do not fill missing frames with invented data or silently retry.
- PC silent mode drops chip writes. Its captures cannot validate sound.

## Commands

```
python3 tools/bootstrap.py
./tools/build-reference.sh /absolute/path/to/user-ROM.md
build/reference/sor --rom /absolute/path/to/user-ROM.md --lang en --hz 60 \
  --silent --debugUtils --port 7777 --auxAddrFile "$PWD/build/reference-missing.txt"
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
| Movement | Four directions, diagonal, plane bounds, jump arcs | Original-ROM smoke captures only |
| Combat | Combo presses/holds, back attack, jump kick, all grabs/throws, police | Not compared |
| Enemy logic | Every family, damage, invulnerability, knockdown, recovery | Not compared |
| Campaign | Scroll triggers/waves, transitions, all bosses, all endings | Not compared |
| Two-player | Join, friendly fire, grabs/assists, lives/continues, scoring | Not compared |
| Randomness/cadence | Same reset/input stream, seeds and per-tick actor state | PC lockstep blocker |

Record verified, inferred and inaccurate behavior separately. Never substitute
host wall-clock sleep for a deterministic frame input script. Any test using cheats
must label the altered setup and is not evidence of ordinary progression.
