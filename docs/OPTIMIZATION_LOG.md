# Active 60 Hz optimization work

The target remains uninterrupted 60 Hz with original audio. It has **not** been
reached. Physical Dreamcast performance is unverified.

## Current changes under validation

- PSG jumps between divider events and advances muted oscillators arithmetically.
  The noise shift register and hidden tone phase keep running while muted.
- FM caches stable operator parameters, hoists the envelope-phase decision and
  specializes its eight connection algorithms. All chip arithmetic remains integer.
- The recognized DAC program's bus writes are collected at the same sample
  boundaries, then FM renders constant-register spans. Idle mailbox polling is
  advanced once per remaining span. Timer status advances at its original cadence;
  CSM (timer-triggered FM key-on) and unknown drivers retain interleaved synthesis.
- Packed sprite rows replace per-pixel tile-address calculation.
- Ordinary translated RAM/ROM accesses use width-specialized native accessors;
  device calls retain bus width and side effects.
- Frame diagnostics use a bounded 64 KiB RAM log. Automatic replays drain it only
  after the measured window. This removes serial printing from gameplay timing.
  Manual runs retain logs until a caught error; excess messages are counted/dropped.
  `SOR_AUDIO_PROFILE=1` enables occasional synthesis phase timings. Keep it off
  for final frame-deadline qualification.

## Measurements retained locally

Same `phase-aligned-actions.json` replay, first 1,200 gameplay intervals:

| Experiment | Mean loop | p95 bound | Local serial log |
| --- | ---: | ---: | --- |
| Previous native DAC checkpoint | 23.393 ms | 31.0 ms | native-dac-combined-final.log |
| Operator parameter cache / event PSG | 21.878 ms | 29.5 ms | audio-cache-flycast.log |
| 512 KiB FM lookup table | 21.802 ms | 30.0 ms | audio-table-flycast.log |
| Envelope-phase specialization / muted PSG | 20.700 ms | 28.5 ms | audio-clock-flycast.log |
| Deferred serial output | 20.589 ms | 28.5 ms | audio-deferred-flycast.log |
| First block-rendering attempt | 21.300 ms | 29.0 ms | audio-batch-flycast.log |
| Fixed FM algorithms / idle batching | 20.625 ms | 28.5 ms | audio-algorithm-flycast.log |
| Explicit link optimization flags | 20.625 ms | 28.5 ms | audio-link-o3-flycast.log |
| Fused channel clock/output attempt | 21.961 ms | 29.5 ms | audio-fused-flycast.log |
| Timer-safe blocks / packed sprite rows | 20.596 ms | 28.0 ms | audio-timer-flycast.log |

Logs are under ignored `build/logs/`. The large table and fused clock/output
attempt were removed. Explicit `-O3` follows KOS's link flags, but its measured
result was unchanged: flag order alone was not the remaining bottleneck.
Synchronous-log and deferred-log worst frames are not directly comparable.

## Correctness gates passed so far

- Full action replay: 2,546,780 PCM stereo frames and 2,866 RAM snapshots match the
  original native-audio checkpoint byte-for-byte after each retained audio change.
- 1,820,525 PSG samples match an independent divider-tick oracle across muted
  phase, period 0/1, noise modes and register-latch changes (ASan/UBSan).
- DAC integration covers 1,000 frames of commands, BUSREQ, reset, DAC panning,
  low bit, live timers and CSM; PCM, status, sound RAM and stem sums match.
- 65,536 synthetic FM samples match pinned ymfm; 160 renderer scenes match.
- 300,000 native RAM/ROM/MMIO accesses match the C bus oracle, including odd
  offsets, 24-bit mirroring, RAM wrap, ROM rejection and device call widths.

Reproduce with `tools/test-psg-events.sh`, `tools/test-dac-integration.sh`,
`tools/test-ymfm-output.sh`, `tools/test-scene.sh`, and
`tools/test-native-memory.sh` after staging/building the native sources.

Next: measure native memory traffic savings and use the synthesis phase timings
to choose the next change. Audio is still opt-in while deadlines and underruns fail.

## Native indexed plane textures

PowerVR now consumes 4-bit indexed plane tiles. Four palette selectors share the
same 64 KiB tile storage; CRAM changes update 64 palette entries instead of
re-expanding every referenced tile. Sprite priority layers remain separate.
This follows the pinned KOS palette API and its Morton texture layout; see
https://kos-docs.dreamcast.wiki/group__pvr__pal__mgmt.html .

- Plane VRAM: 1,048,576 → 65,536 bytes (960 KiB saved).
- Same action replay with the then-current audio: first 600 gameplay intervals
  displayed 600 flips / 600 VBlanks. The complete 1,653-interval run still missed
  refreshes: 1,721 VBlanks / 1,653 flips, mean 17.420 ms, 164 stream underruns.
- First gameplay renderer block's maximum upload time: 13.935 → 4.338 ms;
  free VRAM increased from 3,136,104 to 4,119,144 bytes.
- `tools/test-pvr-tiles.sh`: 524,288 decoded palette indices match the original
  pixels, including transparent index zero (ASan/UBSan).
- Background Flycast capture inspected at `build/palette-flycast.png`: stage,
  HUD and character appear correctly. A full pixel comparison of GPU captures
  remains outstanding; the CPU scene oracle is still the rendering regression.

Benchmarks now run from an immutable copy in `build/flycast-run/`. Rebuilding a
CDI that Flycast is reading can mix sectors from two builds; one failed replay
was discarded for this reason. The launcher records the image SHA-256 in
`build/logs/flycast-run.json`. It still uses background launch flags.
