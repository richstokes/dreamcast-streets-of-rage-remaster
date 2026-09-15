# Progress

## 2026-09-15 — native checkpoint, not a completed game

### Runs

- Both requested repositories and all four submodules pinned; Genesis Plus GX
  added as a separate reference-only tool.
- User ROM matches SoR1 JUE rev00, checksum 9409. No game data committed.
- PC reference compiles after a reproducible correction to invalid auxiliary
  entry points. Boots through menus into Round 1. A staged wakeup patch removes
  the observed lockstep stall, but repeated PC runs still differ.
- Native KOS/SH-4 C++ debug ELF and CDI build and boot in working local Flycast.
  Original intro, menus, Round 1 background/HUD and player render. Narrowing the
  generation seed repair fixed missing actors; gameplay parity is unverified.
- Two-pad frame playback reaches gameplay mode 0016 without unresolved dispatches
  or unmapped-bus faults in the observed session.

### Verified

- Headless backend runs the same simulation, MMIO, software renderer and replay
  as Dreamcast. Two 1,556-frame runs produce 1,557 identical WRAM snapshots.
- Resetting the fallback instruction counter at explicit frame waits removed
  spurious VBlanks. All 30 walking increments and the endpoint (800 to 875)
  now match the original smoke replay. Intermediate frame phase is not identical.
- Two-player menu selection and a 2,158-frame first-encounter probe run in both
  original and native backends. Both players and enemies become active; no native
  bus faults or unresolved dispatches occur. P2 ending X differs (1050 vs 1060),
  so this is coverage, not combat parity.
- KOS services are separated into a platform backend. Diagnostics now run on
  the simulation thread, avoiding concurrent state reads and monitor lifetime risk.

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

- Latest platform-separated link: text 2,332,504 B, data 5,820 B, BSS 283,720 B.
  This is static section size, not measured peak main RAM.
- Early Round 1 heap in use about 1,269,472 B; free PVR memory 5,527,240 B.
  Heap usage is not a complete main-RAM peak or stack high-water measurement.
- Unoptimized Round 1 rasterization about 75 ms; pixel conversion about 18 ms;
  total render/present about 97 ms. Far from 60 Hz.
- Color lookup reduced conversion to about 7.2 ms. Experimental plane rendering
  was about 79 ms and total rendering about 90 ms; it was rejected. These samples
  are not a full frame-time distribution or a worst-case campaign measurement.

### Known limitations

- Original sound/AICA playback not integrated; target is deliberately silent.
- Native cadence is partially compared: walking increments/end position match,
  but boot/menu timing, jump phase and combat state still differ.
- PC lockstep now completes but is nondeterministic; it is not a correctness oracle.
- No enhanced sprites, animation sets, environment art, effects or comparison captures.
- No full-stage/ending coverage, two-player combat parity, loading-stall or audio tests.
- No physical hardware measurements.

### Next concrete milestone

Resolve original-mode cadence, verify controls/combat against
original-ROM traces, replace expensive graphics work using measured results,
then integrate original audio. Enhanced art starts only after the first section
has verified behavior. The full eight rounds and endings remain required.
