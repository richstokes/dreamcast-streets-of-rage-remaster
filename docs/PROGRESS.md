# Progress

## 2026-09-15 — native checkpoint, not a completed game

### Latest: experimental original-audio path

- Replaced discarded sound writes with deterministic YM2612/PSG synthesis and the
  supplied ROM's Z80 drum/voice driver; added a bounded KOS AICA stereo stream.
- Host replay produces 2,546,780 stereo frames without clipping; repeat PCM and
  game RAM are identical. Four SH-4 PCM checkpoint hashes match the host.
- All 1,481 phase-aligned action observations still match original-ROM gameplay.
  Disabled mode preserves the earlier 2,159-snapshot regression trace.
- Default-build Flycast regression: 1,200 gameplay flips / 1,200 VBlanks,
  16.725 ms mean CPU-loop interval. Sanitized audio, replay, core and 160-scene
  renderer tests pass. Manual ELF and CDI are rebuilt with audio disabled.
- **Audio is opt-in (`SOR_AUDIO=1`), not release-ready.** It currently costs too
  much CPU (~38–40 ms gameplay loops) and starves streaming. Ordinary builds keep
  the silent 60 Hz checkpoint. Profiling identifies DAC interpretation, FM and
  PSG synthesis as the next optimization targets. See AUDIO.md for reproduction,
  licenses, buffers, measurements and remaining fidelity limitations.

### Previous: phase-aware original-ROM comparisons

- Added SRP2 bounded state gates shared by Dreamcast and host playback. Gates read
  WRAM and release pads; they never change simulation memory. SRP1 remains compatible.
- Diagnosed the old jump/P2 endpoint discrepancy as opposite halves of the ROM's
  two-VBlank update cycle at the fixed cold-boot input boundary. Explicit mailbox-2
  anchors align tests without modifying physics or concealing boot-time differences.
- Original ROM vs native: 1,481 directional/action observations and 761 two-player
  encounter observations match positions, states, health, camera and lives.
  Collision IDs and fixed-point position/velocity regions match too. The police
  special consumes one stock and locks controls for 637 frames in both backends.
- All 2,865 action replay frames match software-rendered pixels; repeat run has
  2,866 identical RAM snapshots. Existing fixed-frame replay preserves its 2,159
  snapshots. Sanitized replay parsing, timeout and pad-release tests pass.
- Spawn timers/flags and some unclassified object bytes still differ. These tests
  establish sampled behavior, not full RAM, cold-boot or complete combat parity.
- Flycast action replay: first 1,200 gameplay intervals show 1,200 flips over
  1,200 VBlanks, including the special effect; guest CPU-loop mean 16.725 ms.
- Reproduction, limitations and results are in REFERENCE.md and reference/results.

### Previous: native sprite pass and cached PowerVR submission

- `./build-and-run.sh` creates and boots the manual-test `dist/sor-test.elf` with
  the supplied ROM embedded. Full symbols are in `dist/sor-test.debug.elf`.
- Replaced sprite-table decoding on every scanline with one traversal per frame,
  preserving per-line masking, limits, ordering and collision/overflow flags.
- Clear/upload only the union of old/new sprite row extents. Cached aligned
  background packets use one KOS store-queue submission; sprite-only changes
  no longer invalidate background commands. No new SH-4 assembly required.
- **First 600 two-player gameplay intervals: 600 KOS page flips / 600 VBlanks** in
  the final Flycast run. Earlier repeat runs also recorded 600/600. This supports
  one displayed frame per refresh for this checkpoint, not a full-game or retail
  hardware guarantee. Original audio is still absent.
- CPU-loop mean **16.723 ms (~59.8 loops/s)**, p50 <=17 ms, p95 <=22 ms,
  p99 <=29 ms, worst 30.746 ms (previous mean 17.784 ms). Work on either side of
  the graphics wait makes loop intervals vary without necessarily losing a flip.
  Both measurements are preserved in `reference/results/`.
- Renderer block ending at call 1800: mean scene 2.155 ms, upload 1.719 ms,
  command preparation 0.194 ms, submission 0.519 ms, graphics wait 9.979 ms;
  mean sprite transfer 43,211 bytes. This block includes the transition into
  gameplay; it is not the same window as the gameplay-only timing above.
- 160 ASan/UBSan graphics cases pass pixels, VDP flags, crowded scanlines,
  invalid links, tile bounds and partial-upload stale-pixel checks. All 2,158
  replay frames match the software renderer in RGB1555; all 2,159 WRAM snapshots
  remain identical. Actual two-player PowerVR output visually checked in Flycast.
- Final disc link: text 2,345,780 B, data 5,836 B, BSS 1,579,368 B. Gameplay heap
  observed at 2,350,760 B; free VRAM 3,136,104 B. Packet caching adds 960,320 B
  of main RAM. These are not full main-RAM/stack high-water measurements.
- Shadow/highlight, interlace, two-cell vertical scroll and oversized lists still
  use the slow software fallback. Further stages/effects need profiling.

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
- Manual testing uses the embedded test ELF or a CDI packaged without replay.
- Executable translated arithmetic probes: 65,536 ADD.b combinations, wide carry,
  sign/shift boundaries, DIVS overflow and subregister preservation pass on host
  and SH-4 in Flycast.
- An experimental CPU plane renderer matched 96 randomized scenes but did not
  improve measured target time; removed it. Retained the color conversion lookup.
- CD filesystem access and missing-VMU/default-settings path run. VMU write and
  recovery code is not yet exercised on a console.

### Measurements (emulator guest timings; not physical hardware)

- Earlier platform-separated link: text 2,332,504 B, data 5,820 B, BSS 283,720 B.
  This is static section size, not measured peak main RAM.
- Early Round 1 heap in use about 1,269,472 B; free PVR memory 5,527,240 B.
  Heap usage is not a complete main-RAM peak or stack high-water measurement.
- Unoptimized Round 1 rasterization about 75 ms; pixel conversion about 18 ms;
  total render/present about 97 ms. Far from 60 Hz.
- Color lookup reduced conversion to about 7.2 ms. Experimental plane rendering
  was about 79 ms and total rendering about 90 ms; it was rejected. These samples
  are not a full frame-time distribution or a worst-case campaign measurement.

### Known limitations

- Original sound/AICA prototype exists but is too slow and has stream underruns;
  it remains disabled by default. Audio fidelity is not yet established.
- Cold-boot timing and some object bytes differ. Phase-anchored movement, action
  and two-player observations now match; complete combat/campaign parity is open.
- PC lockstep now completes but is nondeterministic; it is not a correctness oracle.
- No enhanced sprites, animation sets, environment art, effects or comparison captures.
- No full-stage/ending coverage, two-player combat parity, loading-stall or audio tests.
- No physical hardware measurements.

### Next concrete milestone

Resolve the remaining startup/object-state differences, extend encounter tests
to grabs/throws, damage/recovery and bosses, and bring original audio within the frame budget without stream underruns. Enhanced art starts only after the first section
has verified behavior. The full eight rounds and endings remain required.
