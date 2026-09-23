# Progress

Newest first. Measurements are Flycast guest timings unless stated; nothing has
run on a real Dreamcast yet.

## 2026-09-23 — weather

Per-round weather in enhanced mode: rain in two parallax sheets with splashes,
wet ground reflecting characters and lights, haze and mist, light shafts and
lightning with its own shadow; its own options-menu switch. The busy two-player
fight stays at every VBlank with it on (OPTIMIZATION_LOG.md, REMASTER.md).

## 2026-09-22 — dynamic lighting

Backdrop windows, neon, lamps and fire become light sources: characters are
shaded across their width with a rim on the lit edge, cast ground shadows that
swing and lengthen as they walk past a light, and get a contact shadow that
lifts in a jump; light spills onto the ground; embers, sparks, smoke, dust and
debris particles. Off with `SOR_LIGHTING=0`, otherwise on with enhanced
graphics (REMASTER.md).

## 2026-09-21 — enhanced art for everything; smooth animation

- `tools/make-art-set.sh` generates 2x art for every player, enemy, boss, weapon,
  item and effect frame in all eight rounds (anti-aliased edges, smoothed
  shading, 8-bit paletted zlib pages) and the Dreamcast loads a round's pages
  only. Hand-made frames replace generated ones with `--override`.
- Art follows the game's fades and flashes; sprite masks (the HUD) are honoured.
- Smooth animation: in-between poses between the game's own frames, with its
  own options-menu setting. The generator accepts few pairs (REMASTER.md).
- The B-button software-renderer toggle is off unless built with
  `SOR_SOFTWARE_TOGGLE=1`.

## 2026-09-19 — fidelity gate met in emulation; enhanced rendering path

- Every criterion of FIDELITY_GATE.md is met in emulation: Round 1 matches the
  original frame for frame from power-on for 9,976 gameplay frames, and
  fourteen state-synchronised windows (48,835 frames: wave 3, the boss, the
  stage clear, throws, weapons, food, continues, game over, two players
  joining, friendly fire and continuing) match in all game RAM. Open: one
  drum command skipped in the two-player window (Z80 bus phase).
- State-synchronised comparisons (`tools/state-sync.py`): the original's
  machine state is exported by the profiling Genesis Plus GX core, the port
  loads it at the matching VBlank wait and both play the same inputs. A
  scripted player (`tools/bot-play.py`) reaches the content no recorded
  replay survives to, then plays unaided through the compared window.
- Found on the way: an untranslated reaction (`$2502`) and player-mask table
  (`$109A8`) now seeded by `tools/audit-dispatch-tables.py`; the Z80 driver
  loader left the decompressed driver out of work RAM; the police special
  caused the one audio underrun (PSG now per channel; VRAM writes tracked).
  Both benchmarks at every VBlank with no underrun.
- Cadence is frame-exact through boot and loads: translated instructions
  charge MC68000 time from the Musashi table, the decompressors their ROM
  routines' time path by path (checked to the cycle by
  `tools/test-decoder-cycles.py`), VINT gating and latency as on hardware,
  and the Z80 runs in step with the 68000, sharing the bus (CADENCE.md).
- Original audio on by default: sequencer, FM, PSG and the ROM's sampled
  drum/voice driver at 60 Hz; effect channels match the original driver's
  state every frame, onsets within 10 ms (AUDIO.md).
- Enhanced rendering path selectable at any time on the same simulation:
  a probe records what each object emitted while the game builds its sprite
  table; art replaces an object in its own depth slot. Options menu with
  cheats (start round, lives, infinite lives/health/specials). Remastered
  title screen.

## 2026-09-18 — audio and renderer performance

Channel-major FM rendering across constant-register spans, forced-blank VDP
DMA timing (a fidelity fix), word-wise render-cache compares, a feeder thread
with clock matching for the AICA stream (~98 ms delay), a PC sampling profiler
(`tools/pc-profile.py`). Release configuration at every VBlank in both
benchmarks (OPTIMIZATION_LOG.md).

## 2026-09-15 — native checkpoint

- Both research repositories pinned (`tools/upstream-lock.json`); the ROM
  validated; the recompiler's entry list repaired from the disassembly
  (REFERENCE.md). Native KOS/SH-4 build boots in Flycast through the intro,
  menus and Round 1 with both players. No 68000 interpreter in the game.
- Genesis Plus GX chosen as the reference (the upstream PC build is
  nondeterministic); scenario replay with state gates, per-frame RAM
  observations and captures on both backends; phase-anchored movement, action
  and two-player comparisons match.
- PowerVR renderer: tile quads with indexed plane textures and a native sprite
  pass; 600 flips over 600 VBlanks in the first gameplay window without audio.
- Original audio prototype (YM2612/PSG synthesis, the ROM's Z80 driver, AICA
  stream), then native DPCM playback and an optional four-channel AICA stem
  path (NATIVE_DAC.md); too slow for 60 Hz at this point and off by default.
