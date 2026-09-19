# Progress

## 2026-09-19 — Round 1 behaviour and slowdown against the original

- Round 1 play (`round1-full`, 26,415 gameplay frames) matches the original ROM
  in every observed field for 8,283 frames: waves 0–1, Garcia-family, Signal and
  Haku-Ro enemies, the police special, the phone booth, a death and respawn.
  Round 1 combat (3,115 frames) and the action replay (1,481) match throughout.
- The original's first two slowdown frames are reproduced on the same updates;
  the third is missed by 163 CPU cycles (0.13% of a frame), where the comparison
  diverges. Found with per-routine cycle histograms of both backends
  (`tools/gpgx-profile-report.py`, `SOR_PC_HISTOGRAM_FRAMES`).
- Cadence model additions (CADENCE.md): exact branch and multiply timing, EXT no
  longer costed as MOVEM, Z80-area wait states, sound-bus DAC-busy retries from a
  shadow DAC driver, and charges for the hand-written object pass, pickup scan,
  joypad sampler and main loop.
- The incremental Nemesis queue now decodes each tile when uploaded, as the
  original does, removing a ~12 ms host burst when art is queued.
- Flycast: gameplay window 1,611 flips / 1,614 VBlanks (was 1,611 / 1,612); one
  77 ms stream underrun at Round 1 start (AUDIO.md); follow-up in TODO.md.
- Next: first-section fidelity gate (two-player beyond 220 frames, grabs/throws,
  later waves and the boss).

## 2026-09-19 — original audio on by default, 60 Hz in Flycast

- Release configuration (audio on, profiling off): action replay 1,659 flips /
  1,659 gameplay VBlanks; two-player encounter 941 / 941. Hardware unverified.
- Sound timing verified against the original ROM (AUDIO.md): effect channels
  match the 68000 driver state every frame; onsets within 10 ms; a 68000 write
  timing bug that stopped notes retriggering is fixed.
- Stream: feeder thread, clock matching, ~98 ms delay (was ~155 ms); underruns
  only during two blanked bulk loads.
- Performance work since 2026-09-15 (OPTIMIZATION_LOG.md): channel-major FM with
  envelope steps in the fast loop, forced-blank VDP DMA timing (a fidelity fix,
  verified against the original), word-wise render-cache compares, VDP LTO.
- New tools: `bench-flycast.sh`, `pc-profile.py` (`SOR_PC_PROFILE=1`),
  `compare-audio.sh`, `compare-sound-state.py`, `ym-render.cpp`.
- Cadence model (CADENCE.md): translated instructions charge MC68000 time from the
  Musashi cycle table, VBlanks follow emulated time, DMA stalls the CPU, and the
  hand-written decompressors charge costs fitted to timings of the ROM's own
  routines. Every game mode now lasts within a frame of the original (loads were
  up to 48 frames short); no gameplay lag; zero stream underruns in Flycast.
  Not frame-exact: a one-frame load difference leaves SoR's upload-VBlank counter
  one behind, which changes one enemy's fall 221 frames into the two-player
  comparison (it matched before only by coincidence). Documented in CADENCE.md.
- Next: behaviour comparisons (first-section movement/combat coverage).

## 2026-09-15 — native checkpoint, not a completed game

### Previous: original audio at 60 Hz

- Optimization continues; the target is not met. Retained audio/renderer changes
  measure 20.596 ms mean over the first 1,200 gameplay intervals, with underruns.
- Direct native RAM/ROM access is implemented and passes 300,000 differential
  accesses plus the full PCM/RAM replay; Dreamcast measurement is in progress.
- `OPTIMIZATION_LOG.md` records measured decisions, removed experiments and tests.

### Previous: native drum/voice decoding and hardware playback evaluation

- Native C++ DPCM playback reduces audio-enabled mean loop time from 29.218 to
  23.393 ms (19.9%) over the first 1,200 gameplay intervals in Flycast.
- An optional four-channel AICA stem backend measures 24.022 ms, so combined
  streaming remains the experimental default. Audio still misses 60 Hz/underruns.
- All reference PCM and gameplay snapshots remain identical; 33 driver cases and
  a 1,000-frame reset/BUSREQ/stem integration test pass with sanitizers.
- Silent-build regression retains 1,200 flips / 1,200 VBlanks (16.725 ms mean).
- See NATIVE_DAC.md for measured memory, hardware decisions and reproduction.
- Next: reduce FM synthesis cost with waveform equivalence checks; continue
  combat-reference coverage. Retail audio fidelity and full-game coverage remain open.

### Previous: reduce audio CPU cost without changing PCM

- Mean audio-enabled loop cost over the first 1,200 gameplay intervals falls from
  39.220 to 29.218 ms (25.5%); p95 falls from 47.0 to 37.5 ms in Flycast.
- Incremental clocks, cached PSG levels, native polling/delay instructions and
  direct FM single-channel output retain all 2,546,780 host stereo frames and
  2,866 RAM snapshots. Four SH-4 PCM hashes match too.
- Sanitized instruction-boundary/state tests and 65,536 synthetic stereo FM
  samples match the upstream implementations. No new audio buffers are needed.
- Default-build regression again records 1,200 flips / 1,200 VBlanks.
- Audio still starves and stays opt-in. Native DAC decoding / AICA sample playback
  and further FM work are the next candidates; AUDIO_OPTIMIZATION.md records the
  hardware split and the fidelity gates. No new ARM firmware or assembly is used.

### Previous: experimental original-audio path

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
