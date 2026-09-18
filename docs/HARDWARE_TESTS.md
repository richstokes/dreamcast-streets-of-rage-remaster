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

## PowerVR renderer checks

- [ ] Compare default PowerVR with Dreamcast B software toggle in the same scene.
- [ ] Check foreground poles, player overlap, sprite limits, palette flashes and window HUD.
- [ ] Check two-player scrolling and every stage's line-scroll/environment effects.
- [ ] Record fallback cases (shadow/highlight, interlace, two-cell vertical scrolling).
- [ ] Run the replay procedure in TOOLCHAIN.md and capture hardware serial output.
- [ ] Compare FRAME_STATS `vblanks`/`flips`; record missed refreshes separately from
      CPU-loop p50/p95/p99/worst (loop intervals can vary without missing a refresh).
- [ ] Collect GPU_STATS phase timings and partial sprite upload bytes.
- [ ] Check disappearing/moving sprites leave no stale rows and background tiles
      becoming visible invalidate cached commands correctly.
- [ ] Confirm new frame/list allocation and 1.75 MiB explicit texture allocation fit retail VRAM.

Use a CDI built without SOR_REPLAY for ordinary play. The embedded-ROM ELF is a
Flycast development convenience; its retail loading path has not been tested.

## Original audio

Default builds play original audio (AUDIO.md). Flycast holds 60 Hz in the
recorded replays; retail performance and AICA output are unverified. Record the
serial `AICA` lines (level, underruns, dropped/repeated frames) and any
`AICA_UNDERRUN` frames.

- [ ] Record AICA available memory, queued frames, underruns and overruns from serial.
- [ ] Compare original FM melody, PSG effects and sampled drums/voices against Genesis.
- [ ] Check stereo channels, mute/pause, music changes and repeated stage loads.
- [ ] Capture frame times with audio enabled and disabled using the same replay.
- [ ] After CPU optimization, run for 30 minutes and check stream clock drift.

## Native DAC / optional AICA stems

See NATIVE_DAC.md for build flags and known performance limits.

- [ ] Compare combined and four-channel output using the same replay; listen for
      stereo phase, changed clipping, missing samples and drum/voice timing.
- [ ] Record AICA_DAC underruns, resyncs, overruns and transfer mean/max timings.
- [ ] Exercise pause, resets and repeated loads; check synchronized recovery after stalls.
- [ ] Measure stack/heap peaks and sound RAM usage on unmodified retail hardware.
