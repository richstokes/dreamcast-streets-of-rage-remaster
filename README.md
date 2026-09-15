# Streets of Rage — native Dreamcast port (work in progress)

A native SH-4/KallistiOS port of SoR1, with a graphical remaster planned after
original gameplay parity. **This is not yet the finished playable remaster.**

## Current checkpoint

- Supplied JUE revision 00 ROM validated and kept outside git.
- Both research repositories recursively cloned and locked to exact commits.
- Recompiler seed defects isolated; PC reference builds, boots and reaches Round 1.
- Native C++ game code cross-compiles to a debug ELF and self-booting CDI.
- Flycast boots the CDI and reaches Round 1 with the original background, HUD and player.
- Frame-counted two-pad diagnostic replay, Maple input boundary, KOS CD loading,
  memory/profiling diagnostics and versioned VMU settings storage implemented.
- Player visibility is verified; gameplay fidelity is not established.
- Target audio is currently silent. No enhanced artwork, all-stage coverage or
  60 Hz performance claim. No physical Dreamcast validation yet.

This executes statically translated SoR code. The Dreamcast target does not contain
a generic Genesis/68000 emulator. It temporarily retains VDP device semantics and
a software graphics fallback while native platform boundaries are established.

## Build and run

Provide your own raw 512 KiB SoR1 World/JUE revision 00 ROM. The accepted SHA-256
is recorded in [reference notes](docs/REFERENCE.md). No game data is downloaded.

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
and are local, ignored artifacts. Direct ELF boot alone lacks the `/cd` game data.
For manual play, package without `SOR_REPLAY`.

Dreamcast controls: D-pad movement, **X attack**, **A jump**, **Y police special**,
Start menus/pause; same mapping on port B. Input behavior still needs verification.

## Verification and tracking

- [Architecture and retail memory budgets](docs/ARCHITECTURE.md)
- [Input audit and attribution](docs/INPUT_AUDIT.md)
- [Reference reproduction and known upstream defects](docs/REFERENCE.md)
- [Progress, measured limits and next milestone](docs/PROGRESS.md)
- [Physical Dreamcast checklist](docs/HARDWARE_TESTS.md)

Run `./tools/test.sh`, `python3 tools/test-rom.py`, and
`python3 tools/test-generation.py`. Arithmetic probes generated
from upstream snippets run at native boot; host sanitizer execution is also supported.

Original source additions use the [MIT license](LICENSE). Research/runtime code
retains its own [notices](licenses). Game assets and ROM-derived code are not
relicensed by this repository. Enhanced source art and asset packages are not yet
implemented; filtered original sprites will not count as the remaster.
