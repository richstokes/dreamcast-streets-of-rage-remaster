# First-section fidelity gate (Round 1)

Enhanced presentation starts only after the original-mode game is shown to
behave like the original ROM through the first section. This page states the
gate's criteria, the evidence for each and what remains. Evaluation 2026-09-19,
commit `d3f5c04` and later; original = the ROM in Genesis Plus GX.

**Status: not yet passed.** Everything from power-on to the start of Round 1,
and Round 1 play for 9,976 frames, matches the original exactly. Weapons, throws
as a checked category, the boss, finishing the round and continue/game over are
not yet compared, and physical hardware is untested.

## Criteria and evidence

| Criterion | Evidence | Status |
| --- | --- | --- |
| Boot, menus, story and loads last as long as the original, frame for frame | Every game mode equal in both backends (`tools/compare-timeline.py`, two-player replay); decompressor time exact per decode | Met |
| Movement in all directions, jumps, attacks and combos match the original | Action replay: all 1,481 frames and all object bytes (`phase-aligned-actions.json`) | Met |
| Round 1 enemies behave as in the original (AI, damage, knockdown) | Round 1 play equal for 9,976 frames: enemy types `$21`–`$25` present; `$21`, `$22` and `$24` hit (32 times) and knocked out (8); Round 1 combat equal for 3,115 frames | Met for waves 0–1 |
| Grabs and throws | 16 grabs in matched windows (grab fields equal); throws not counted separately | Partly met |
| Police special | Used twice in matched windows, control lock and stock equal | Met |
| Player damage, death and respawn | 34 health drops and a death with respawn in the matched Round 1 window | Met |
| Two-player play (join, interaction, shared progression) | Two-player encounter: all 761 frames and object bytes; friendly fire, joint grabs and shared continues not isolated | Partly met |
| Weapons and pickups | No pickup in the matched windows | Not met |
| Round 1 boss, round completion, continue and game over | Beyond the matched Round 1 window | Not met |
| Original audio in step with the game | Effect and music channels match the driver state; drum driver writes match the original's in count and value (AUDIO.md, CADENCE.md) | Met |
| Full speed on the target | Flycast gameplay 1,611 flips over 1,616 VBlanks with audio; one stream underrun at round start (TODO.md); hardware untested | Partly met |

Coverage figures: `reference/results/behaviour-coverage-2026-09-19.json`
(`tools/behaviour-coverage.py`); comparisons:
`reference/results/behaviour-round1-2026-09-19.json`.

## What blocks the remaining items

A comparison stays exact only while every gameplay update finishes on the same
side of its VBlank as the original's. Round 1 play diverges at relative frame
9,977, where the original's update ends 79 cycles before the VBlank and native's
a few hundred cycles later. Native timing now tracks the original to an RMS of
about 490 cycles per update (CADENCE.md), so long runs meet such near-ties.

Two ways forward:

1. **Shorter scenarios for the missing behaviour** (weapons and pickups,
   throws, friendly fire, continue), each reaching its event within the frames
   that stay exact.
2. **State-synchronised comparisons** for content far into the round (the boss,
   completion): start both backends from the same machine state (68000
   registers, work RAM, VDP, Z80 and sound state) at a VBlank wait and compare
   the following window. This isolates behaviour from accumulated timing
   differences; it would be labelled as a comparison technique, not play.

Closing the timing gap further (exact per-path costs for the remaining
hand-written routines, DIV and register-shift timing, YM2612 busy from the Z80's
own writes) extends every exact window but will not by itself reach the end of
the round.
