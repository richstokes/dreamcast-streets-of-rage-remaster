# Retail Dreamcast test sheet

Status: **physical hardware unverified**. Human can fill this in asynchronously.
Do not block emulator development on these results. This checkpoint is not a finished game.

Record console model/region, video cable/mode, controller/VMU models, disc or GDEMU
version, artifact SHA-256, git commit and toolchain revision for each run.

## Boot and media

- [ ] Fresh source + pinned tools + locally supplied ROM rebuild successfully.
- [ ] CDI boots on a retail console capable of MIL-CD, with no extra RAM.
- [ ] GDEMU reads all assets from the image's ISO9660 `/cd` filesystem.
- [ ] VGA and NTSC television output show the same 4:3 gameplay area.
- [ ] Missing/wrong game data gives a visible useful error; no silent hang.

## Controllers / VMU

- [ ] Port A and B connect, disconnect and reconnect safely.
- [ ] Two-player join, independent buttons, friendly fire, grabs/throws work.
- [ ] Settings load with no VMU, unformatted/full VMU and corrupt data.
- [ ] Settings save/load succeeds and keeps a prior valid slot if interrupted.
- [ ] VMU removal during a write reports failure without affecting gameplay.
- [ ] The game does not write saves during active combat.

## Fidelity (not yet verified on emulator either)

- [ ] Compare recorded movement/jump/combo/grab/throw/special scenarios with Genesis.
- [ ] Compare damage/invulnerability/knockdown/recovery timings.
- [ ] Verify all eight rounds, every boss, both ending branches and two-player progression.
- [ ] Verify scores, extra lives, continues and records.
- [ ] Original and enhanced graphics use identical game-state traces.
- [ ] Every replacement animation has correct pivots, feet and active-frame alignment.

## Performance / endurance

- [ ] Log real hardware presentation and simulation frame times separately.
- [ ] Report p50/p95/p99/max and missed 16.67 ms deadlines; don't use average FPS alone.
- [ ] Measure code/data/BSS, heap peak, each stack's high-water mark and free main RAM.
- [ ] Measure PVR allocation peaks, upload bytes/time and vertex-list overflow.
- [ ] Measure AICA memory use and stream underruns; listen for clicks and missing channels.
- [ ] Stress two players, maximum enemies, police effects and boss encounters.
- [ ] Repeat stage loads 50 times; verify allocations return to the same baseline.
- [ ] Finish an uninterrupted full playthrough and both endings.

## Result log

| Date / tester | Console + media | Commit / artifact hash | Result + evidence | Issue |
| --- | --- | --- | --- | --- |
| Pending | Pending | Pending | No physical measurements yet | All boxes open |
