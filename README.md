# Streets of Rage — Dreamcast port and remaster

A native Dreamcast (SH-4, KallistiOS) port of Streets of Rage 1, built from the
original game's code rather than an emulator, with an optional enhanced
graphics mode on top.

You need your own copy of the ROM. Nothing derived from it is in this repository.

## Status

- **Original mode** plays from power-on with the original music and sound.
  Round 1 matches the original ROM frame for frame in emulation, including the
  boss, weapons, continue/game over and two-player play
  ([docs/FIDELITY_GATE.md](docs/FIDELITY_GATE.md)). All eight rounds run to the
  ending but rounds 2–8 have not been compared with the original.
- **Enhanced mode** replaces every player, enemy, boss, weapon and effect sprite
  with 2x art, and can add dynamic lighting and shadows. The art comes from an
  offline redraw of the original frames; hand-drawn art is not started
  ([docs/REMASTER.md](docs/REMASTER.md)).
- Full speed with no audio dropouts in Flycast. **Not yet tested on a real
  Dreamcast.**

## What you need

- **The ROM**: Streets of Rage / Bare Knuckle (World), revision 00, 524,288 bytes,
  as a plain big-endian Mega Drive dump (no header, not byte-swapped).
  SHA-256 `dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0`.
  Other revisions are rejected. Put it here:

  ```text
  original_rom/Bare Knuckle - Ikari no Tetsuken ~ Streets of Rage (World).md
  ```

  Or pass its path as the first argument to the build scripts.
- **KallistiOS** with an SH-4 GCC that includes C++ (`enable_cpp=1` in kos-chain).
  The scripts source `~/.local/share/dreamcast/kos/environ.sh`; set `KOS_ENV` if
  yours is elsewhere. Versions used: [docs/TOOLCHAIN.md](docs/TOOLCHAIN.md).
- **mkdcdisc** ([gitlab.com/simulant/mkdcdisc](https://gitlab.com/simulant/mkdcdisc))
  to make the disc image. Expected at `build/mkdcdisc/build/mkdcdisc`, or set
  `MKDCDISC`.
- **Python 3.14** (`python3.14` on the path, or set `PYTHON`) for code generation.
- **Somewhere to run it**: the Flycast emulator, or a Dreamcast with a GDEMU
  (or similar ODE) or a burned CD-R. The image is a normal self-booting CDI,
  but so far it has only been run in Flycast, so expect the first hardware run
  to turn things up ([docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)).
  `tools/run-flycast.sh` launches Flycast on macOS, looking for `Flycast.app`
  in `~/.local/share/dreamcast/flycast/` then `/Applications/` (set
  `FLYCAST_BIN`); elsewhere, open the image in Flycast yourself.

To see what is set up and what is missing, with where to get each thing:

```bash
tools/check-requirements.sh all
```

Every build script runs the same checks for what it needs before starting. The
first build also clones the research repositories listed in
`tools/upstream-lock.json` (the SoR decompilation and Genesis Plus GX) into
`research/`.

## Build a disc image

```bash
./build-cdi.sh
```

This checks the ROM, generates the game code, cross-compiles, and writes
`dist/sor.cdi`. Copy it to your GDEMU card, burn it, or load it in Flycast — on
macOS:

```bash
./tools/run-flycast.sh dist/sor.cdi
```

`dist/` contains your game data. Keep it out of git and don't share it.

## Quick run without a disc

For development there is a faster loop that builds an ELF with the ROM embedded
and launches Flycast directly (macOS):

```bash
./build-and-run.sh
```

Flycast starts muted; `FLYCAST_MUTE=0 ./build-and-run.sh` plays sound. Serial
output goes to `build/logs/flycast.log`.

## Enhanced graphics

The enhanced art package is built from your ROM, not downloaded. It needs the
host build, a Genesis Plus GX reference core and numpy + Pillow, and takes
about two minutes:

```bash
tools/build-headless.sh
tools/build-profile-core.sh
python3 -m venv build/tools-venv && build/tools-venv/bin/pip install numpy pillow
tools/make-art-set.sh
```

Once `build/art/SORART.PAK` exists, `build-cdi.sh` puts it on the disc and
`build-and-run.sh` embeds it in the ELF. The disc build starts in original
graphics unless built with `SOR_ENHANCED=1`; `build-and-run.sh` starts enhanced
unless `SOR_ENHANCED=0`. Either way the options menu switches at any time.
Without the package the game plays in original graphics only. Details, and how
to override frames with your own art: [docs/REMASTER.md](docs/REMASTER.md).

## Controls

| Dreamcast | Action |
| --- | --- |
| D-pad | Move |
| X | Attack |
| A | Jump |
| Y | Police special |
| Start | Menus / pause |
| L + R | Options menu |

The same mapping works on port B for player 2.

**L + R** opens the options menu at any time, including mid-game (the game
pauses). Up/down selects, left/right or A changes, B / Start / L + R closes.

- **Start round** (1–8) and **starting lives** (1–9), for the next new game.
- **Infinite lives / health / specials**, for both players.
- **Graphics**: original or enhanced. **Animation**: smooth adds in-between
  poses where the art package has them. **Lighting**: dynamic adds light and
  shadows from the backdrop. **Weather**: rain, wet ground, haze, mist and
  lightning by round, with dynamic lighting. These change only what is drawn,
  never the game.
- **Restore defaults.**

Settings last for the session. Dynamic lighting is on by default (with enhanced
graphics); `SOR_LIGHTING=0` at build time starts without it. `SOR_SMOOTH=1` and
`SOR_WEATHER=1` start with those options on.

## Tests

```bash
./tools/test.sh
python3 tools/test-rom.py
python3 tools/test-generation.py
./tools/test-scene.sh
```

Comparisons against the original ROM, benchmarks and the full method are in
[docs/REFERENCE.md](docs/REFERENCE.md), [docs/CADENCE.md](docs/CADENCE.md),
[docs/AUDIO.md](docs/AUDIO.md) and [docs/OPTIMIZATION_LOG.md](docs/OPTIMIZATION_LOG.md).
Progress notes: [docs/PROGRESS.md](docs/PROGRESS.md). Hardware checklist:
[docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md).

## Licence

Original code in this repository is [MIT](LICENSE). The research repositories
and runtime keep their own [notices](licenses). The game, its ROM and anything
generated from it (translated code, extracted frames, art packages, disc images)
are not covered by this licence and must not be redistributed.
