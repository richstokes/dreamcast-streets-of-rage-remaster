Build a native Dreamcast port and graphical remaster of the original Genesis/Mega Drive **Streets of Rage**, using these repositories as research and implementation inputs:

* https://github.com/RuiNelson/StreetsOfRageProject
* https://github.com/gsaurus/sor-disassemblies

Use **KallistiOS, native C/C++, and SH-4 assembly wherever each makes sense**. Start with KallistiOS for bootstrapping, hardware access, controllers, audio, filesystems, and VMU support. Use assembly only when profiling identifies a worthwhile bottleneck or a hardware interface requires it. Do not rewrite working systems in assembly merely for novelty.

The goal is faithful original gameplay with substantially improved graphics on an **unmodified retail Dreamcast**, delivered as a reproducibly buildable, playable game—not just a technical demonstration.

## 1. Audit the inputs before choosing the architecture

Clone both repositories, including required submodules, and record the exact revisions used.

Determine:

* How complete the SoR1 recompilation actually is.
* Which routines are reconstructed C++, generated code, untranslated assembly, or unresolved.
* Which systems still depend on emulated Genesis hardware.
* Whether code generation assumes x86, host endianness, pointer sizes, alignment, or compiler behaviour incompatible with SH-4.
* How sprites, animation states, collision boxes, levels, enemy logic, and audio are represented.
* Relevant licenses and asset requirements.

Treat repository descriptions and AI-generated annotations as claims to verify against code and runtime behaviour.

Use the disassembly as supporting evidence; do not assume it is portable source. Do not expand scope to SoR2 or SoR3.

Produce a short architecture decision and then implement it. Do not stop at a plan.

If the original ROM or another essential input is missing, specify exactly what is needed. Do not obtain copyrighted game data yourself or commit supplied ROMs to the repository. Continue any useful work that does not require the missing input.

## 2. Establish a reproducible gameplay reference

Build and run the upstream PC implementation where possible. Compare it against the original game in a Genesis emulator; the upstream implementation must not be treated as automatically correct.

Use scriptable debugging, input playback, memory inspection, screenshots, and traces where available. Identify repeatable scenarios covering:

* Movement, jumping, attacks, combos, grabs, throws, and special attacks.
* Enemy behaviour, damage, invulnerability, knockdowns, and recovery.
* Camera scrolling, enemy waves, stage transitions, and bosses.
* Two-player interactions, lives, scoring, and progression.

Record reference inputs and meaningful state observations. Keep a distinction between verified behaviour, inferred behaviour, and known inaccuracies.

Preserve the original simulation cadence, arithmetic semantics, random behaviour where relevant, and combat timing. Choose and document the initial supported ROM region/version.

## 3. Build the faithful Dreamcast version

Create a maintainable separation between:

* Gameplay simulation.
* Rendering and asset lookup.
* Audio.
* Input.
* Storage and platform services.

Retain useful generated code initially if it behaves correctly and fits the hardware. Replace expensive or restrictive Genesis hardware emulation incrementally. A compatibility layer may be an intermediate step, but a generic Genesis emulator running a ROM is not the final deliverable.

Implement:

* A reproducible KallistiOS cross-build.
* A bootable Dreamcast image and a debug ELF.
* Controller support, including local two-player play.
* Working gameplay, original graphics, and audio.
* Appropriate VMU persistence for settings and records, with graceful behaviour when no VMU is present.
* Asset loading that works from the intended CD/GDEMU filesystem without desktop paths or arbitrary SD-card access assumptions.

Target the retail limits: **16 MiB main RAM, 8 MiB video RAM, and 2 MiB sound RAM**. Budget code, stacks, heaps, graphics, audio, and I/O buffers separately. Do not assume memory expansion.

Get one playable section working first, then carry the faithful implementation through all stages and endings. The first section is a checkpoint, not the final scope.

## 4. Implement the graphical remaster

After the first section behaves correctly, use it to develop the enhanced presentation. Keep original and enhanced rendering selectable so that gameplay can be compared using the same simulation.

Art direction: retain the original’s gritty neon city, distinctive silhouettes, and readable combat, with the polish of a high-quality Dreamcast-era 2D game.

Implement enhancements in this order:

1. **Redrawn characters**

   * Aim initially for roughly twice the original sprite dimensions where memory permits.
   * Preserve gameplay-space size, feet positions, animation anchors, facing, and attack reach.
   * Keep hitboxes and damage timing independent of replacement artwork.
   * Cover complete animation sets, including grabs, throws, knockdowns, and recovery.

2. **Redrawn environments**

   * More detailed backgrounds and foregrounds.
   * Additional parallax where it does not obscure gameplay.
   * Animated signs, windows, lights, and suitable environmental motion.
   * Preserve collision boundaries, camera triggers, and encounter placement.

3. **Effects**

   * Improved hit sparks, smoke, shadows, particles, and restrained lighting overlays.
   * Use techniques supported efficiently by PowerVR.
   * Avoid shader-dependent desktop effects unless replaced with a practical Dreamcast technique.

4. **Animation polish**

   * Add presentation frames where useful without changing simulation timing.
   * Preserve attack anticipation, active frames, recovery, and hit-stop.
   * Avoid smoothing that weakens the original combat feedback.

5. **Presentation and sound**

   * A coherent upgraded HUD and menus.
   * Clean audio mixing and an asset pipeline capable of streaming replacement music.
   * Preserve the original soundtrack option. Do not make a newly produced soundtrack a prerequisite for finishing.

Start with **4:3 output, targeting 640×480 where performance allows**. Do not widen the gameplay area by default: that changes enemy activation, camera behaviour, and encounter balance.

If image-generation tools are available, use them for concepts and production assistance, then validate and clean the results. A contact sheet or a few attractive keyframes do not count as usable animation assets. Check frame consistency, transparency, dimensions, pivots, and visual alignment.

Do not substitute filtered or automatically enlarged original sprites and call the graphical remaster complete.

## 5. Engineer the asset pipeline around the console

Build repeatable tools for extracting reference data, importing replacement artwork, packing textures, and validating animation metadata.

Use appropriate texture formats, palettes, compression, and stage-specific loading. Do not preload the entire game’s enhanced artwork.

Measure the cost of each enhancement. Doubling sprite width and height quadruples pixel storage before format changes or additional frames.

Ensure texture loading and audio streaming do not cause combat hitches. Maintain a lower-cost enhanced configuration if needed.

Keep source art separate from generated runtime assets, and document how to rebuild the asset packages.

## 6. Validate correctness and performance

Use Dreamcast emulation for rapid iteration and a physical Dreamcast when available.

Measure:

* Frame-time distribution and worst-case scenes.
* Main RAM and VRAM peaks.
* Texture upload and asset-loading stalls.
* Audio underruns.
* Two-player scenes with many enemies and effects.
* Stability across repeated stage loads and a full playthrough.

Aim for a stable **60 Hz presentation for the initial NTSC target**, while preserving the original game’s simulation behaviour. Do not claim performance based solely on a desktop emulator’s apparent speed.

If physical hardware is unavailable, clearly label hardware performance and compatibility as unverified and provide a reproducible test procedure.

Add focused tests for risky translated arithmetic, state transitions, asset bounds, and deterministic gameplay comparisons. Prioritise actual game behaviour over tests that merely repeat implementation details.

## 7. Deliver and continue autonomously

Work in reviewable commits. Maintain a concise progress log with:

* What runs.
* What has been verified.
* Performance and memory measurements.
* Known defects.
* The next concrete milestone.

Make routine implementation decisions yourself. Ask only when a missing input or a major creative choice genuinely blocks progress. If a subsystem is incomplete, isolate it and keep progressing on independently useful work.

Final deliverables:

* Source repository with upstream revisions and attribution.
* Reproducible build and asset-processing instructions.
* Debug ELF and bootable Dreamcast image when supplied assets permit.
* Original and enhanced presentation modes.
* Playable coverage of the complete original game.
* Comparison captures demonstrating the graphical improvements.
* Test results and an explicit list of remaining limitations.

Do not declare completion because the title screen, one character, or the first stage works. If blocked, report the precise blocker and preserve a runnable checkpoint.

Begin by auditing the two repositories, building the reference implementation, and establishing the smallest faithful gameplay section that can run on Dreamcast.

