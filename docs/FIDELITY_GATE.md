# First-section fidelity gate (Round 1)

Enhanced presentation starts only after the original-mode game is shown to
behave like the original ROM through the first section. This page states the
gate's criteria, the evidence for each and what remains. Evaluation 2026-09-19,
commit `d3f5c04` and later; original = the ROM in Genesis Plus GX.

**Status: met in emulation; physical hardware untested.** Everything from
power-on to the start of Round 1, and Round 1 play for 9,976 frames, matches the
original exactly. From shared machine states (state-synchronised comparison,
below) the rest of Round 1 matches too, in fourteen windows and 48,835 frames:
wave 3, throws, every Round 1 weapon and food, the boss, the stage clear into
Round 2, continuing and game over, and two-player play (joining, friendly fire,
a continue while the other player plays on). Both Flycast benchmarks run at
every VBlank with no audio underrun. One audio timing difference remains (a
skipped drum hit, below).

## Criteria and evidence

| Criterion | Evidence | Status |
| --- | --- | --- |
| Boot, menus, story and loads last as long as the original, frame for frame | Every game mode equal in both backends (`tools/compare-timeline.py`, two-player replay); decompressor time exact per decode | Met |
| Movement in all directions, jumps, attacks and combos match the original | Action replay: all 1,481 frames and all object bytes (`phase-aligned-actions.json`) | Met |
| Round 1 enemies behave as in the original (AI, damage, knockdown) | From power-on, 9,976 frames (waves 0–2; types `$21`–`$25`; 32 hits, 8 knockouts). Synced: 95 hits and 22 knockouts on `$21`, `$22`, `$24`, `$26` and the boss | Met |
| Grabs and throws | 16 grabs from power-on and 53 synced; 13 throws synced (4 from the front, state `$62`; 9 from behind after a vault, `$70`), none in any recorded replay | Met |
| Police special | Used twice from power-on and once synced; control lock and stock equal | Met |
| Player damage, death and respawn | 34 health drops and a death with respawn from power-on; synced: 81 health drops and 7 deaths | Met |
| Two-player play (join, interaction, shared progression) | Encounter replay: all 761 frames and object bytes. Synced: 3,926 frames of two-player play (12 friendly-fire hits, player 2 grabbing player 1, 7 throws by both), player 2 joining a one-player game with Start, and player 2 continuing while player 1 plays on | Met |
| Weapons and pickups | Synced: every Round 1 weapon — the bat (`$0A`, dropped by the enemy carrying it) picked up twice and used, the knife (`$08`) picked up and thrown, the bottle (`$09`) — and food eaten three times | Met |
| Round 1 boss | Synced: Antonio (`$56`) and his boomerang (`$96`) for 4,000 frames, all game RAM equal | Met |
| Round completion | Synced: Antonio knocked out, stage clear (modes `$18`/`$1A`), Round 2 intro and its first wave | Met |
| Continue and game over | Synced: high-score name entry, the continue prompt, a continue taken, and one declined through game over, the top-10 screen and back to the Sega logo and intro | Met |
| Original audio in step with the game | Effect and music channels match the driver state; drum driver writes match the original's in count and value (AUDIO.md, CADENCE.md). One drum hit skipped in the two-player window (below) | Met, one known timing difference |
| Full speed on the target | Flycast, audio on: action replay 1,611 flips over 1,611 VBlanks, two-player 893 over 893, no stream underrun (OPTIMIZATION_LOG.md); hardware untested | Met in emulation |

Coverage figures: `reference/results/behaviour-coverage-2026-09-19.json`
(`tools/behaviour-coverage.py`); comparisons:
`reference/results/behaviour-round1-2026-09-19.json` (from power-on) and
`reference/results/state-sync-2026-09-19.json` (synced).

## Why comparisons from power-on stop at 9,976 frames

A comparison stays exact only while every gameplay update finishes on the same
side of its VBlank as the original's. Round 1 play diverges at relative frame
9,977, where the original's update ends 79 cycles before the VBlank and native's
a few hundred cycles later. Native timing tracks the original to an RMS of
about 490 cycles per update (CADENCE.md), so long runs meet such near-ties.

## State-synchronised comparison

This is a comparison technique, not play: it shows that from the same machine
state the port behaves as the original, independent of timing differences
accumulated before it (`tools/state-sync.py`, REFERENCE.md).

1. The original's machine state is exported at the end of a frame (68000
   registers, work RAM, VDP registers and memories, Z80 RAM and registers; the
   YM2612 and PSG are not read by the game and are not transferred). The frame
   must be one where the 68000 waits for VBlank in the main loop and no
   incremental Nemesis stream is in flight, whose decoder the port keeps in host
   state.
2. The native port plays the scenario from power-on to its first matching
   VBlank wait in Round 1, loads that state and plays the original's inputs
   from the export frame on.
3. Work RAM is compared frame by frame. RAM the port represents differently is
   left out (incremental Nemesis registers, the Nemesis code table, stack below
   the main loop's frame). Frames where both captures were taken while an update
   or a load was still running (mailbox `$FFFA00` zero) are listed separately:
   how far each side got by the VBlank is timing, and a difference in behaviour
   would persist into the next settled frame. During loads the port's
   decompressors write their output at once and charge the ROM routine's time
   afterwards, so a mid-load capture shows output a frame or two early.

| Window (original frame) | Content | Frames | Result |
| --- | --- | --- | --- |
| `round1-full` 10,384 | wave 2 | 3,000 | diverges at 977 (the known near-tie at 9,977) |
| `round1-full` 18,685 | wave 3: Garcia `$20`, Nora `$26`, food | 3,000 | equal (2 unsettled frames) |
| `round1-full` 20,584 | wave 3, knife, food, a throw | 3,000 | equal (1 unsettled) |
| `round1-full` 23,485 | wave 3, last death, continue prompt | 3,000 | equal |
| `round1-bot-throws` 3,600 | throw, bottle picked up | 4,000 | equal |
| `round1-bot` 11,497 | Antonio fight, two deaths | 4,000 | equal |
| `round1-bot-clear` 14,900 | Antonio knocked out, stage clear, Round 2 start | 3,000 | equal (29 unsettled, during the two loads) |
| `round1-bot-items` 15,900 | knife picked up and thrown, food, four throws | 4,500 | equal |
| `round1-bot` 16,900 | last death, name entry, continue taken | 3,000 | equal |
| `round1-bot-gameover` 16,900 | name entry, continue declined, game over, top 10, logo, intro | 3,000 | equal (33 unsettled, during loads) |
| `two-player-bot` 2,000 | two players, friendly fire, grabs and throws | 5,000 | equal but one sound-driver byte for 14 frames from 3,926 |
| `round1-bot-join` 2,900 | player 2 joins with Start, throws, friendly fire | 3,000 | equal |
| `round1-bot-bat` 14,050 | the bat dropped, picked up twice and used; the knife | 4,335 | equal |
| `two-player-bot-continue` 23,900 | player 2 out of lives, continues while player 1 plays | 3,000 | equal |

No recorded replay survives to the boss, so the bot windows come from
`tools/bot-play.py`: a scripted policy plays the original (throws, pickups and
the continue prompt are options) and, until a set frame, RAM writes keep the
player standing and hold ordinary enemies at one hit point. The writes stop
before each export, so every compared frame is the game playing the recorded
inputs on its own. The two-player and join runs use no writes (player 2 attacks
player 1 for 200 frames in every 900); the two-player continue run keeps only
player 1 standing, until 23,800. The bot's inputs are saved as ordinary scenarios
(`reference/scenarios/round1-bot*.json`, `two-player-bot*.json`).

The windows found these differences in the port:

- **Z80 driver loader** (`$1061C`): the ROM decompresses the driver into work
  RAM at `$FF7000` before copying it to the Z80; the port decoded it in host
  memory only. The port now leaves the same bytes in RAM.
- **Untranslated state-table targets**: two-player friendly fire reached player
  reaction `$2502`, which the recompiler had not emitted as an entry, and the
  port stopped. `tools/audit-dispatch-tables.py` lists every state-table target
  without an entry; the 47 that are decoded code are now seeded
  (`tools/generate.py`), and the remaining four are data past a table's end.
  The two-player continue then reached `$109DC`, a player-mask jump table
  (`jmp table(pc,d0)` at `$109A8`, 1 = player 1, 2 = player 2, 3 = both) whose
  "both" entry the disassembly never reached. The audit now reads such inline
  tables as well, and 13 more targets are seeded.
- **A skipped drum hit** (two-player window, original frame 5,925): the 68000's
  sound driver sends a drum command only if Z80 RAM `$1FF6` shows the previous
  sample finished. The port's Z80 finished that 29-frame sample 66,000 master
  clocks (0.25%) later than the original's and missed the check by 34,000;
  the drum was dropped and the channel state converged 14 frames later. The
  sample's 2,626 DAC writes are identical in value; the drift comes from the
  68000's bus-acquire retries, which quantise small Z80 phase differences into
  whole 847-clock retries (the port retried 10 times where the original retried
  8). Exact Z80/68000 bus interleaving is the remaining timing item below.

## What remains

- **Timing**: exact Z80/68000 bus interleaving for the sound-bus acquire loop
  (the skipped drum above), exact per-path costs for the remaining hand-written
  routines, DIV and register-shift timing, and YM2612 busy from the Z80's own
  writes. These extend exact windows from power-on and remove the drum skip.
- **Hardware**: a run on a physical console, including frame time and audio.
