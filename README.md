# Streets of Rage — native Dreamcast port (work in progress)

A native SH-4/KallistiOS port of SoR1 with a selectable graphical remaster on top
of the unchanged original game. **This is not yet the finished playable remaster:**
it runs in Flycast only so far, the enhanced art is machine-generated rather than
hand-drawn, and only Round 1 has been verified against the original.

## Current state (2026-09-21)

**Original mode**

- The game runs from power-on through menus into play, with the original audio
  (YM2612, PSG and the ROM's Z80 drum/voice driver) on by default.
- Round 1 meets the [first-section fidelity gate](docs/FIDELITY_GATE.md) in
  emulation. Against the original ROM in Genesis Plus GX: every game mode from
  power-on to Round 1 lasts exactly as long, Round 1 play matches for 9,976
  frames, and fourteen state-synchronised windows (48,835 frames) match in all
  game RAM — throws, every Round 1 weapon and food, the boss, the stage clear
  into Round 2, continue and game over, and two-player play.
- Flycast, audio on: the action replay (1,611 gameplay frames) and the two-player
  replay (893) present a frame at every VBlank with no audio underrun.
- A cheats/options menu (L + R) starts any of the eight rounds. Scripted sweeps
  clear every round, Round 8 through Mr. X to the ending, but Rounds 2–8 have
  not been compared with the original.

**Enhanced mode** ([docs/REMASTER.md](docs/REMASTER.md))

- Original and enhanced graphics are selectable at any time on the same
  simulation. Enhanced mode draws the three players (all 205 frames) and the
  enemies, bosses, weapons, items and effects of all eight rounds with 2x art:
  940 frames, loaded per round and per character to fit PowerVR memory.
- The art is generated offline from the original frames by a redraw pipeline;
  it is **not hand-drawn**. Any frame can be overridden with hand-made art.
  Art follows the game's fades and flashes and honours sprite masks; anything
  without art loaded falls back to the original pieces.
- LIGHTING: DYNAMIC adds light taken from the backdrop, cast and contact
  shadows, light spill on the ground, and particles for fire and hit sparks,
  all within the PowerVR's fixed pipeline.
- ANIMATION: SMOOTH shows in-between poses where the package has them.
  Automatic generation failed for this art (955 of 975 pose pairs rejected), so
  in practice this needs hand-made in-betweens; the runtime and packing exist.
- Backgrounds, the HUD, text, title, menus and cutscenes are unchanged.

**Open**

- No run on a physical Dreamcast yet: frame time, audio and VMU writes are
  untested on hardware ([checklist](docs/HARDWARE_TESTS.md)).
- One known audio timing difference: a drum hit skipped in one two-player
  window (Z80/68000 bus phase; docs/FIDELITY_GATE.md).
- Rounds 2–8, both endings and an uninterrupted full playthrough are not
  verified against the original. Round 8, and rounds 5 and 6 with two different
  characters, want more art than fits; the least important pages fall back.
- Hand-drawn art, redrawn environments, HUD/menus and replacement music are
  not started.

This executes statically translated SoR code. The Dreamcast target does not contain
a generic Genesis/68000 emulator. It retains VDP device semantics and a PowerVR tile
renderer; in original mode sprite evaluation uses a single SAT traversal with
per-line limits on the CPU and PowerVR composites the layers. 68000 and Z80 time is
modelled so that VBlank cadence, slowdown and sound timing follow the original
([docs/CADENCE.md](docs/CADENCE.md)).

## Required ROM

Supply your own **Streets of Rage / Bare Knuckle (World/JUE), revision 00** ROM.
The build requires this exact dump:

| Property | Required value |
| --- | --- |
| Format | Raw big-endian Mega Drive ROM; no copier header, byte swapping or interleaving |
| Size | **524,288 bytes (512 KiB)** |
| Product code | `MK 00001019-00` |
| Region header | `JUE` (initial execution target: overseas NTSC) |
| ROM checksum | `9409` |
| SHA-256 | `dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0` |

For the default launcher, place it at:

```text
original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md
```

The filename alone does not establish compatibility. Verify the contents with:

```sh
python3 tools/rom.py "original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md" --require-known
```

A different filename/location is fine when passed explicitly, for example
`./build-and-run.sh "/absolute/path/to/your-ROM.bin"`. Other revisions or modified
ROMs are not supported by the current generated code; the build rejects a hash
mismatch. ROMs are not supplied or downloaded by this project. Keep your ROM and
ROM-containing build artifacts out of git and do not redistribute them.

## Build and run

On this machine, run:

```sh
./build-and-run.sh
```

This uses the supplied ROM in `original_rom`, builds `dist/sor-test.elf`, and starts
it in Flycast. An explicit ROM path can be passed as the first argument. The test
ELF embeds the ROM for direct boot; it needs no CDI. Full debug symbols remain in
`dist/sor-test.debug.elf`. Both files contain your game data and stay out of git.
Repeated runs replace this project's previous emulator instance. Flycast starts
muted; `FLYCAST_MUTE=0 ./build-and-run.sh` plays sound.

Enhanced graphics need the art package, which is derived from your ROM and built
locally (about 2 minutes; prerequisites in [docs/REMASTER.md](docs/REMASTER.md)):

```sh
tools/make-art-set.sh
```

When `build/art/SORART.PAK` exists, the test ELF embeds it and starts in enhanced
graphics (`SOR_ENHANCED=0` starts in the original), and the disc image carries it
as a file. L + R opens the options menu, where GRAPHICS switches between the two
at any time, ANIMATION turns on in-between poses (`SOR_SMOOTH=1` starts with it
on) and LIGHTING adds shadows and light from the scene (`SOR_LIGHTING=1`). Without
the package the game plays in original graphics only.

To regenerate the latest game code and build a self-booting CDI image, run:

```sh
./build-cdi.sh
```

This writes `dist/sor.cdi` without launching Flycast. It uses the same default ROM
as `build-and-run.sh`; pass a ROM path as the first argument or set `SOR_ROM` to
use another location. It also writes `dist/sor.elf` and `dist/SHA256SUMS`.

For the reproducible reference build and CD/GDEMU image:

Use the exact ROM specified above. See [reference notes](docs/REFERENCE.md) for
validation and comparison details.

```
python3 tools/bootstrap.py
./tools/build-reference.sh /absolute/path/to/user-ROM.md
./tools/build-dreamcast.sh
./tools/package.sh /absolute/path/to/user-ROM.md
./tools/run-flycast.sh
```

Requires KallistiOS, SH-4 GCC **with C++**, Python 3.14, CMake/SDL3 for the host,
and mkdcdisc for disc packaging. See [toolchain setup](docs/TOOLCHAIN.md).

Outputs: `dist/sor.elf`, `dist/sor.cdi`, `dist/SHA256SUMS`. Images include your ROM
and are local, ignored artifacts. The disc-build ELF alone lacks `/cd` data; use `build-and-run.sh` for a self-contained test ELF.
For manual play, package without `SOR_REPLAY`. Flycast launches with macOS
background/hidden flags as a best effort; the headless reference creates no window.

Dreamcast controls: D-pad movement, **X attack**, **A jump**, **Y police special**,
Start menus/pause; same mapping on port B. Builds made with
`SOR_SOFTWARE_TOGGLE=1` let **Dreamcast B** on port A switch to a slow software
comparison renderer; it is off by default. Controls are untested on a physical pad.

Press **L + R on port A** to open the **Cheats** and options menu, including during play.
The game pauses while the menu is open. Use D-pad up/down to select, left/right
or **A** to change, and **B / Start / L + R** to return.

- **Start round:** any of the eight rounds, from its beginning.
- **Starting lives:** 1–9 for each player. Round and lives apply to the next new game.
- **Infinite lives**, **infinite health**, and **infinite specials:** apply to both
  players when you resume. Falls still cost a life; police support remains unavailable
  in Round 8, as in the original game.
- **Graphics** (original / enhanced), **Animation** (original / smooth) and
  **Lighting** (original / dynamic): presentation only; they apply at once and
  never change the simulation. Animation and lighting need enhanced graphics.
- **Restore defaults:** Round 1, three lives, extra cheats off.

Cheat settings last for the current session and are not written to the VMU.
All cheats default to off.
For feature checks, run `./tools/test-cheats.sh` and, after building the headless
runner, `python3 tools/test-cheats-gameplay.py /absolute/path/to/your-ROM.md`.
These checks cover all eight starts and two-player cheats; they do not establish
full-game fidelity.

## Verification and tracking

- [Graphical remaster: art pipeline, smooth animation, dynamic lighting](docs/REMASTER.md)
- [First-section fidelity gate: criteria and evidence](docs/FIDELITY_GATE.md)
- [Cadence model](docs/CADENCE.md), [audio](docs/AUDIO.md) and the
  [optimization log](docs/OPTIMIZATION_LOG.md)
- [Architecture and retail memory budgets](docs/ARCHITECTURE.md)
- [Input audit and attribution](docs/INPUT_AUDIT.md)
- [Reference reproduction and known upstream defects](docs/REFERENCE.md)
- [Progress, measured limits and next milestone](docs/PROGRESS.md)
- [Physical Dreamcast checklist](docs/HARDWARE_TESTS.md)

Run `./tools/test.sh`, `python3 tools/test-rom.py`, and
`python3 tools/test-generation.py`, and `./tools/test-scene.sh`. Arithmetic probes generated
from upstream snippets run at native boot; host sanitizer execution is also supported.

Original source additions use the [MIT license](LICENSE). Research/runtime code
retains its own [notices](licenses). Game assets and ROM-derived code are not
relicensed by this repository. The current enhanced art package is generated from
the ROM's own frames, so it stays under `build/` and out of git like any other
ROM-derived artifact. It is a stand-in: filtered original sprites do not count as
the remaster, and hand-made art is still to come.
