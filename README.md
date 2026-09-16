# Streets of Rage — native Dreamcast port (work in progress)

A native SH-4/KallistiOS port of SoR1, with a graphical remaster planned after
original gameplay parity. **This is not yet the finished playable remaster.**

## Current checkpoint

- Supplied JUE revision 00 ROM validated and kept outside git.
- Both research repositories recursively cloned and locked to exact commits.
- Recompiler seed defects isolated; PC reference builds, boots and reaches Round 1.
- Native C++ game code cross-compiles to a debug ELF and self-booting CDI.
- Flycast boots the CDI and reaches Round 1 with the original background, HUD and player.
- Repeatable traces and phase-anchored original-ROM checks for movement, jump
  actions, police special and a two-player encounter (see docs/REFERENCE.md).
- Frame-counted two-pad diagnostic replay, Maple input boundary, KOS CD loading,
  memory/profiling diagnostics and versioned VMU settings storage implemented.
- Player visibility is verified; gameplay fidelity is not established.
- Optimized two-player Flycast checkpoint presents one frame per VBlank in the
  measured replay window. Full-game and physical Dreamcast performance unverified.
- Target audio is currently silent. No enhanced artwork or all-stage coverage.

This executes statically translated SoR code. The Dreamcast target does not contain
a generic Genesis/68000 emulator. It temporarily retains VDP device semantics and
a PowerVR tile renderer with a software comparison/fallback path. Sprite evaluation uses a single SAT traversal
with per-line limits on the CPU; PowerVR composites the layers.

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
Repeated runs replace this project's previous emulator instance.

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
Start menus/pause; same mapping on port B. **Dreamcast B** on port A toggles
PowerVR/software rendering for comparison. It does not reset gameplay. Input
behavior still needs full verification.

## Verification and tracking

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
relicensed by this repository. Enhanced source art and asset packages are not yet
implemented; filtered original sprites will not count as the remaster.
