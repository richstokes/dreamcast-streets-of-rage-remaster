# Toolchain and local builds

## Current reproducible inputs

- KallistiOS `cd340378043f00cd1d05284ee0d02548d33d6054` (v2.3.0 local build).
- SH-4 GCC 15.2.0, binutils 2.45.1, newlib 4.6.0.20260123.
- C++ frontend required (`--enable-languages=c,c++`, `--enable-threads=kos`).
- mkdcdisc `a663882c4ac0e2123c6229af6dd596cee7a66dc1` from
  <https://gitlab.com/simulant/mkdcdisc.git>, built with Meson/Ninja and libisofs.
- Python 3.14 for upstream generation; SDL3/CMake/C++23 for PC reference.
- Source locks for both inputs and Genesis Plus GX: `tools/upstream-lock.json`.

The preinstalled local compiler was C-only. The C++ frontend and standard library
were built from the local KOS-patched gcc-15.2.0 source using:

```
make -C "$KOS_BASE/utils/kos-chain" -o fixup-newlib -o fetch-gcc \
  build-gcc-pass2 enable_cpp=1 enable_objc=0 enable_objcpp=0 makejobs=6 erase=0 \
  gcc_pass2_configure_args='--disable-plugin --with-sysroot --with-native-system-header-dir=/usr/include --with-system-zlib'
```

This command skips existing dependencies and is **only for an already installed,
matching C/newlib toolchain**, not a fresh bootstrap. For a fresh toolchain use
KOS kos-chain's documented full build with C++ enabled and the versions above.
`--disable-plugin` addresses a GCC host-plugin build failure with this Mac's SDK;
it does not disable game features. Existing local KOS networking source changes
were pre-existing and were not edited. A clean-SDK rebuild still needs verification.

## Build

```
python3 tools/bootstrap.py
./tools/build-reference.sh /absolute/path/to/user-ROM.md
./tools/build-dreamcast.sh
./tools/package.sh /absolute/path/to/user-ROM.md
./tools/run-flycast.sh
```

Use `KOS_ENV`, `PYTHON`, `JOBS`, `MKDCDISC`, `FLYCAST_BIN` to override local paths.
The game reads `/cd/SOR.BIN`; direct ELF boot cannot supply this file by itself.
The debug ELF is `build/native/sor.elf`; the playable-image candidate is
`dist/sor.cdi`. Both are ignored. No downloaded or reconstructed ROM is included.
`SOURCE_DATE_EPOCH` fixes disc timestamps (default set in package.sh).
Reproducible source builds are supported; bit-for-bit ELF reproducibility across
host paths/toolchains has not been established.

The installed `/Applications/Flycast.app` aborts on a SH-4 context assertion on
this machine. The launcher prefers the working local patched build at
`~/.local/share/dreamcast/flycast/Flycast.app`. This is a host emulator issue, not
a successful console boot. Native boot was verified using the latter.

## Frame-counted diagnostic replay

```
SOR_REPLAY="$PWD/reference/scenarios/boot-movement.json" \
  ./tools/package.sh /absolute/path/to/user-ROM.md
./tools/run-flycast.sh
```

This embeds `/cd/REPLAY.BIN`, overrides both physical controllers and releases all
buttons when the replay ends. Build again without SOR_REPLAY for manual play.
Replay is developer instrumentation, not an attract mode. It does not prove the
port's frame cadence equals the Genesis reference.

## Tests

`./tools/test.sh` checks memory/bus bounds, endian/wrap semantics and save corruption
under AddressSanitizer/UBSan. `tools/arithmetic-probes.py` emits executable tests
from the actual upstream opcode snippets. The native build runs 65,536 ADD.b
cases plus carry, sign, wide shifts, DIVS overflow and subregister cases at boot.
The same probes passed host sanitizers. None substitutes for combat comparison.
