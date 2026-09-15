# Progress

## 2026-09-15 — native checkpoint, not a completed game

### Runs

- Both requested repositories and all four submodules pinned; Genesis Plus GX
  added as a separate reference-only tool.
- User ROM matches SoR1 JUE rev00, checksum 9409. No game data committed.
- PC reference compiles after a reproducible correction to invalid auxiliary
  entry points. Boots through menus into Round 1; lockstep remains unreliable.
- Native KOS/SH-4 C++ debug ELF and CDI build and boot in working local Flycast.
  Original intro, menus, Round 1 background/HUD and player render. Narrowing the
  generation seed repair fixed missing actors; gameplay parity is unverified.
- Two-pad frame playback reaches gameplay mode 0016 without unresolved dispatches
  or unmapped-bus faults in the observed session.

### Verified

- Host ASan/UBSan memory/endian/bus-bound tests and save corruption tests pass.
- Generation now rejects a lost manual sprite entry; the regression test covers
  both an unreachable entry and a valid forwarding alias. ROM bounds tests pass.
- Latest local CDI is packaged without replay for manual controller testing.
- Executable translated arithmetic probes: 65,536 ADD.b combinations, wide carry,
  sign/shift boundaries, DIVS overflow and subregister preservation pass on host
  and SH-4 in Flycast.
- An experimental CPU plane renderer matched 96 randomized scenes but did not
  improve measured target time; removed it. Retained the color conversion lookup.
- CD filesystem access and missing-VMU/default-settings path run. VMU write and
  recovery code is not yet exercised on a console.

### Measurements (emulator guest timings; not physical hardware)

- Initial linked text 2,681,060 B, data 5,820 B, BSS 281,672 B (before later probes).
- Early Round 1 heap in use about 1,269,472 B; free PVR memory 5,527,240 B.
  Heap usage is not a complete main-RAM peak or stack high-water measurement.
- Unoptimized Round 1 rasterization about 75 ms; pixel conversion about 18 ms;
  total render/present about 97 ms. Far from 60 Hz.
- Color lookup reduced conversion to about 7.2 ms. Experimental plane rendering
  was about 79 ms and total rendering about 90 ms; it was rejected. These samples
  are not a full frame-time distribution or a worst-case campaign measurement.

### Known limitations

- Original sound/AICA playback not integrated; target is deliberately silent.
- Simulation/interrupt cadence not yet compared against Genesis.
- PC lockstep times out; experimental barrier change did not fix it and was not retained.
- No enhanced sprites, animation sets, environment art, effects or comparison captures.
- No full-stage/ending coverage, two-player combat parity, loading-stall or audio tests.
- No physical hardware measurements.

### Next concrete milestone

Resolve original-mode cadence, verify controls/combat against
original-ROM traces, replace expensive graphics work using measured results,
then integrate original audio. Enhanced art starts only after the first section
has verified behavior. The full eight rounds and endings remain required.
