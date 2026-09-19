# Streets of Rage Dreamcast port and remaster

Checklist derived from [the original brief](ORIGINAL_PROMPT.md), including the
requested Flycast workflow, on-demand image builds, hardware tracking, and pushes
to `main`. Initial status reconciled with [progress notes](docs/PROGRESS.md) on
2026-09-15. This is an early native checkpoint, not a finished playable remaster.

**Status:** `[x]` means the specific result has recorded evidence. `[ ]` means
unfinished or unverified, including partially implemented work. Check off an item
only after its acceptance condition is met; record tests, measurements, defects,
and relevant commit/capture paths in the progress log.

## Working rules

- Scope is the original SoR1 only; use the disassembly as evidence, not portable source.
- Use native C/C++ and KallistiOS for platform services. Add SH-4 assembly only
  for a measured bottleneck or a hardware requirement.
- Target an unmodified retail console: **16 MiB main RAM, 8 MiB VRAM, 2 MiB AICA RAM**.
- Preserve gameplay cadence, arithmetic, randomness, hitboxes and combat timing.
- Keep the original gameplay area and 4:3 output; target 640×480 where feasible.
- Never download copyrighted game data or commit the supplied ROM/extracted assets.
- Launch emulators and test windows **in the background**, without taking focus.
- Make routine decisions autonomously; ask only about essential missing inputs or
  a genuinely blocking major creative choice. Continue independent work when blocked.
- Work in reviewable commits and push progress to `main` as requested.
- Physical-console testing is asynchronous and must not block emulator development.
- A title screen, one character or one stage is a checkpoint, not completion.

## Next milestone: faithful original-mode gameplay section

- [ ] Resolve reference lockstep timeouts and obtain repeatable comparison traces.
- [ ] Establish and verify native simulation/interrupt cadence against Genesis.
  (2026-09-19: model established and verified to ±1 frame per mode; not frame-exact —
  see docs/CADENCE.md and reference/results/cadence-2026-09-19.json.)
- [ ] Verify movement and combat in the first section, including two-player play.
- [x] Replace measured rendering bottlenecks and remeasure target performance.
  (2026-09-19, Flycast: 1,659/1,659 and 941/941 gameplay flips/VBlanks with audio; OPTIMIZATION_LOG.md.)
- [x] Integrate original audio and verify sound timing.
  (Default on; effect channels match original driver state every frame, onsets within 10 ms;
  reference/results/audio-timing-2026-09-19.json. Music start offset follows the cadence item.)
- [ ] Pass the first-section fidelity gate before starting enhanced presentation.

## 1. Input audit and architecture

- [x] Clone both requested repositories and required recursive submodules.
- [x] Record exact revisions in [the upstream lock](tools/upstream-lock.json).
- [x] Build an initial source audit distinguishing manual C++, generated code,
  original assembly/data, unresolved dispatch paths and hardware dependencies.
- [x] Identify initial portability risks: endian handling, fixed-width arithmetic,
  atomics, desktop threads/SDL, memory allocation and compiler semantics.
- [ ] Complete SH-4 portability verification, including pointer size, alignment,
  compiler assumptions and behavior across all executed routines.
- [x] Generate code from the supplied ROM and record translated/stub counts;
  repair invalid seeds using disassembly evidence and guard the sprite-entry regression.
- [ ] Establish actual runtime coverage and recompilation completeness; zero
  generated stubs does not establish full-game correctness.
- [x] Document initial object/sprite, animation, level and sound representation findings.
- [ ] Complete the data map for collision boxes, animation/state tables, enemy logic,
  encounter streams, bosses, endings and audio, corroborated by runtime observations.
- [x] Record licenses, attribution, asset requirements and unresolved licensing questions.
- [ ] Resolve any outstanding redistribution requirements before distributing affected artifacts.
- [x] Validate and hash the supplied 512 KiB World/JUE revision 00 ROM; select overseas NTSC.
- [x] Keep ROM, generated game data and research checkouts excluded from git.
- [x] Write the [architecture decision](docs/ARCHITECTURE.md) and implement an initial native facade.
- [ ] Update provisional architecture/audit statements as new runtime evidence supersedes them.

## 2. Reproducible gameplay reference

- [x] Build and run the PC implementation after the documented entry-list repair.
- [x] Run the original ROM in the separate Genesis Plus GX reference harness.
- [x] Add frame-counted input playback, RAM observations and reference captures.
- [x] Record a boot/movement/attack/jump smoke scenario; treat it as limited evidence.
- [ ] Fix PC lockstep capture and establish repeatable reset/input/state alignment.
- [ ] Compare PC and native results against the original ROM, not just each other.
- [ ] Record verified behavior, inferred behavior and known inaccuracies separately.
- [ ] Verify original arithmetic semantics, random-state evolution and simulation cadence.
- [ ] Capture meaningful state observations and repeatable inputs for every scenario below.

### Required behavior comparisons

- [ ] Movement in all directions, diagonals and movement boundaries.
- [ ] Jump arcs, jump attacks and landing.
- [ ] Basic attacks, combos, back attacks, anticipation, active frames, recovery and hit-stop.
- [ ] Grabs, throws and special/police attacks.
- [ ] Each enemy family and boss: logic, damage, invulnerability, knockdown and recovery.
- [ ] Camera scrolling, encounter activation, waves and stage transitions.
- [ ] Two-player joining, interactions, friendly fire, grabs/assists and shared progression.
- [ ] Lives, continues, scoring, records, game over and ending branches.

## 3. Faithful native Dreamcast game

### Platform and build

- [x] Cross-compile statically translated gameplay using KallistiOS and SH-4 C++.
- [x] Produce a debug ELF and bootable CDI; verify boot in local Flycast.
- [x] Provide an on-demand image build script: [tools/package.sh](tools/package.sh).
- [ ] Verify a clean checkout and clean SDK can reproduce the build from documented inputs.
- [ ] Establish and document artifact reproducibility, including any host-path/toolchain limits.
- [x] Replace desktop memory/platform orchestration with the initial native facade.
- [ ] Finish maintainable boundaries between simulation, rendering/asset lookup,
  audio, input, storage and platform services.
- [ ] Incrementally replace expensive/restrictive Genesis hardware emulation;
  retain useful generated code only where behavior and memory cost are acceptable.
- [x] Keep a generic Genesis/68000 emulator out of the Dreamcast executable.
- [x] Load the supplied data from `/cd/SOR.BIN` in the bootable image.
- [ ] Verify final stage asset loading from CD and GDEMU without desktop or SD-path assumptions.

### Gameplay, input, audio and persistence

- [x] Render original intro/menus and Round 1 background, HUD and player in Flycast.
- [x] Implement Maple input mappings and two-pad diagnostic playback.
- [ ] Verify physical controller input and local two-player gameplay end to end.
- [ ] Complete faithful gameplay with original graphics and audio in the first section.
- [ ] Implement and verify original sound effects, music, mixing and AICA output.
- [x] Implement versioned settings/save encoding with corruption tests.
- [x] Implement VMU alternate-slot storage and exercise missing/invalid-default behavior.
- [ ] Connect VMU persistence to real settings and records.
- [ ] Verify VMU writes, reloads, full/missing/corrupt devices, interrupted saves and recovery.
- [x] Document separate initial memory reservations for code, stacks/runtime, simulation,
  graphics, audio, I/O and headroom; distinguish reservations from measured peaks.
- [ ] Measure actual peaks and enforce all retail memory limits through the campaign.

### Complete campaign coverage

Each round requires playable completion with verified encounters, boss behavior,
transitions, original audio and one-/two-player coverage; simply reaching it is insufficient.

- [ ] Round 1.
- [ ] Round 2.
- [ ] Round 3.
- [ ] Round 4.
- [ ] Round 5.
- [ ] Round 6.
- [ ] Round 7.
- [ ] Round 8.
- [ ] All original endings and relevant one-/two-player branches.
- [ ] Uninterrupted complete playthrough and progression/save verification.

## 4. Graphical remaster — after the first-section fidelity gate

- [ ] Make original and enhanced rendering selectable using the same simulation.
- [ ] Establish gritty neon-city art direction, distinctive silhouettes and readable combat.
- [x] Establish initial 4:3 640×480 output without widening the gameplay area.
- [ ] Validate the final output/performance configuration on the retail target.

### 4.1 Redrawn characters

- [ ] Produce genuinely redrawn characters, initially aiming for roughly 2× sprite dimensions
  where memory allows; filtered/upscaled originals do not satisfy this requirement.
- [ ] Preserve gameplay-space size, feet, pivots, animation anchors, facing and attack reach.
- [ ] Keep hitboxes and damage timing independent of replacement art.
- [ ] Cover complete player, enemy and boss animation sets, including grabs, throws,
  knockdowns and recovery; keyframes/contact sheets do not count as complete assets.
- [ ] Use available image generation for concepts/production assistance, then clean and
  validate frame consistency, transparency, dimensions, pivots and alignment.

### 4.2 Redrawn environments

- [ ] Complete detailed backgrounds and foregrounds for all stages.
- [ ] Add readable parallax and appropriate animated signs, windows, lights and scenery.
- [ ] Preserve collision boundaries, camera triggers and encounter placement.

### 4.3 Effects

- [ ] Improve hit sparks, smoke, shadows, particles and restrained lighting overlays.
- [ ] Implement effects efficiently on PowerVR; replace desktop shader dependencies.

### 4.4 Animation polish

- [ ] Add presentation frames where useful without changing simulation timing.
- [ ] Verify anticipation, active frames, recovery and hit-stop remain unchanged.
- [ ] Verify interpolation/smoothing preserves combat feedback and readability.

### 4.5 Presentation and sound

- [ ] Complete coherent upgraded HUD and menus.
- [ ] Provide clean mixing and streaming support for replacement music.
- [ ] Preserve a working original soundtrack option; a new soundtrack is not a completion gate.

## 5. Console asset pipeline

- [ ] Build repeatable reference-data extraction tools.
- [ ] Build replacement-art import, texture packing and animation-metadata validation tools.
- [ ] Select practical texture formats, palettes and compression using measured costs.
- [ ] Implement stage-specific loading/residency; never preload the entire enhanced game.
- [ ] Budget every enhancement, including the 4× pixel-storage cost of doubling both dimensions.
- [ ] Prevent texture uploads, asset reads and audio streaming from hitching combat.
- [ ] Provide a lower-cost enhanced configuration if required by measurements.
- [ ] Separate source art from generated runtime packages and document package rebuilding.
- [ ] Validate asset bounds, frame coverage, atlas coordinates, pivots and package integrity.

## 6. Correctness and performance validation

- [x] Establish Flycast iteration with native serial diagnostics and timing instrumentation.
- [x] Record initial code/heap/VRAM and renderer timings; current renderer misses 60 Hz badly.
- [x] Pass focused host memory/endian/bus-bound and save-corruption tests.
- [x] Pass translated arithmetic probes on host and SH-4 in Flycast.
- [x] Add ROM-input rejection and manual sprite-entry routing regression tests.
- [ ] Add focused gameplay state-transition, asset-bounds and deterministic comparison tests.
- [ ] Measure frame-time distributions and worst-case scenes, not just averages or apparent speed.
- [ ] Measure main RAM, stack, heap, VRAM and sound-RAM peaks separately.
- [ ] Measure texture-upload and asset-loading stalls.
- [x] Measure audio underruns and sound synchronization.
  (Serial AICA/AICA_UNDERRUN counters, stream delay ~98 ms; AUDIO.md, OPTIMIZATION_LOG.md.)
- [ ] Stress two-player scenes with many enemies and effects.
- [ ] Test stability across repeated stage loads and complete playthroughs.
- [ ] Achieve stable 60 Hz NTSC presentation while preserving original simulation behavior.
- [x] Create an asynchronous [physical Dreamcast checklist](docs/HARDWARE_TESTS.md)
  and label hardware performance/compatibility unverified.
- [ ] Incorporate the human's retail Dreamcast/CD/GDEMU test results as available;
  preserve explicit unverified labels for anything not tested.

## 7. Delivery and ongoing tracking

- [x] Maintain source locks, attribution and a reviewable source repository.
- [x] Maintain a progress log covering what runs, evidence, measurements, defects and next milestone.
- [x] Provide initial build instructions and local debug ELF/CDI checkpoint artifacts.
- [ ] Complete clean-build and asset-processing instructions for the finished game.
- [ ] Deliver final original and enhanced presentation modes.
- [ ] Deliver playable coverage of the complete original game and all endings.
- [ ] Produce matched comparison captures demonstrating actual graphical improvements.
- [ ] Publish final test results, measured budgets and an explicit remaining-limitations list.
- [ ] Verify final deliverables satisfy the full brief; do not close this checklist at a demo checkpoint.

When blocked, record the precise blocker and preserve a runnable checkpoint.
Keep this checklist and the progress log synchronized as verified work lands.
