# Active 60 Hz optimization work

The target is uninterrupted 60 Hz with original audio. In Flycast both
benchmarks now reach it (2026-09-19, below); physical Dreamcast performance is
unverified.

## 2026-09-19 — police-special peak: PSG and VRAM tracking

The last stream underrun (action replay, "Round 1 start" in earlier notes) came
from the police special, and the two-player encounter had two more. A profiler
window over those frames (`SOR_PC_PROFILE_FRAMES=FIRST:LAST`, gameplay frames)
showed the CPU saturated and PSG generation at 11.6% of it: the special's noise is
clocked by tone 2 at period 1, so the time-ordered edge loop stepped four or five
edges per sample through all channels, calling `setPolarity` for each.

- PSG output is summed per channel (linear, so identical to the time-ordered
  walk); muted tones only advance their dividers unless tone 2 clocks audible
  noise, when only its rising edges are recorded; the noise register runs in
  locals; and a fully muted PSG is advanced to its next write in one step.
- VDPState counts VRAM writes and marks each written 32-byte tile (write, fill,
  copy, reset, state load). The scene cache compares that generation instead of
  64 KiB of VRAM, and the renderer re-checks only marked tiles.

Output: every AUDIO_PCM digest and both full-replay audio captures unchanged;
2,863 + 2,157 GPU scenes validated against the software renderer.

| Build | Action VBlanks / flips | Underruns | Ring min | Two-player VBlanks / flips | Underruns | Ring min |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| Before | 1,616 / 1,611 | 1 | 46 | 902 / 893 | 2 | 79 |
| Per-channel PSG | 1,615 / 1,611 | 1 | 31 | — | — | — |
| + muted tones advance only | 1,613 / 1,611 | 1 | 211 | — | — | — |
| + muted-span skip | 1,613 / 1,611 | 0 | 150 | — | — | — |
| + tone-2 rising edges only | 1,612 / 1,611 | 0 | 834 | 894 / 893 | 1 | 406 |
| + noise in locals | 1,612 / 1,611 | 0 | 829 | 894 / 893 | 0 | 107 |
| + VRAM write tracking | **1,611 / 1,611** | **0** | 1,222 | **893 / 893** | **0** | 1,058 |

The mixing pass (PSG included) at frame 2,399 went from 2.91 to 1.66 ms; with the
PSG silent from 1.06 ms to 0.09 ms. Forcing ymfm's `clamp`/`bitfield` inline
changed nothing measurable and was dropped. Logs: `build/logs/{baseline-0919b,
psg-*,noise-*,vram-*}-flycast.log`.

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
| Indexed plane textures (below) | 17.626 ms | 23.0 ms | palette-flycast.log |
| Burst DAC stepping / fixed FM algorithms | 17.134 ms | 21.5 ms | resume-switch-flycast.log |
| Burst DAC / single generic FM algorithm | 17.287 ms | 22.0 ms | resume-compact-flycast.log |
| Channel-major FM spans, calls out of line | 17.320 ms | 22.5 ms | span-flycast.log |
| Channel-major FM spans, inlined step/output | 16.864 ms | 20.5 ms | span-inline-flycast.log |

Logs are under ignored `build/logs/`. The large table and fused clock/output
attempt were removed. Explicit `-O3` follows KOS's link flags, but its measured
result was unchanged: flag order alone was not the remaining bottleneck.
Synchronous-log and deferred-log worst frames are not directly comparable.

Burst DAC stepping runs the recognized driver once per video frame instead of
once per output sample; each YM write keeps the sample boundary of its original
instruction start clock. Complete DAC program steps execute natively; partial
steps retain their original cycle timing. Over the whole 1,653-interval replay
it reduced missed refreshes from 56 to 29 and underruns from 142 to 99
(against `audio-pointer-flycast.log`). Its full replay output matches the
native-audio checkpoint byte-for-byte (PCM and every RAM snapshot).
Replacing the eight specialized FM algorithms with the generic path was slower
(40 missed refreshes, 118 underruns, busiest-frame FM output 5.30 → 5.70 ms) and
was reverted. Repeated runs of the same image produced identical Flycast counters,
so single A/B runs are meaningful here. `tools/bench-flycast.sh <name>` packages
the replay, runs it in background Flycast and keeps `build/logs/<name>-flycast.log`.

Channel-major FM spans (`src/audio/ymfm_sor_span.ipp`): between register writes
and periodic prepares, channels share only the envelope counter and LFO. The
span renderer records per-sample LFO values, then runs each channel across the
whole span with its operators hot; prepare samples keep the original path.
The phase-only operator step and the algorithm output are forced inline, so one
channel's loop fits the SH-4 instruction cache. Without that inlining the span
was slower (42 missed refreshes); with it, over the whole replay: 10 missed
refreshes (was 29), 51 underruns (was 99), busiest profiled frame synthesis
10.6 → 7.1 ms. `tools/test-ymfm-output.sh` now renders the staged build through
random span splits against pinned per-sample ymfm, including LFO AM/PM, DAC
toggles, SSG-EG and 4096-sample prepares (205,765 samples); stale-LFO and
silent-channel-offset mutants fail it. Full replay PCM and RAM remain byte-exact.

## Missed refreshes outside the gameplay window

`FRAME_STATS` measures gameplay only. The AICA report now also records frames
produced and consumed, and VBlanks since the stream started; `SLOW` lines log
every interval spanning more than one VBlank, with synthesis, presentation and
remaining (game logic) time and the mode word. Whole-replay results:

| Change | Missed VBlanks | Underruns | Silence inserted | Log |
| --- | ---: | ---: | ---: | --- |
| Span renderer (stream started with ~700 frames of cushion) | 106 | 51 | — | span-inline-flycast.log |
| 2,048-frame stream cushion; refill it on shortfall | 106 | 30 | 89,229 | stream-diag-flycast.log |
| Forced-blank VDP DMA rates | 38 | 8 | 22,647 | vdp-blank-dma-flycast.log |
| Division-free HV counters | 32 | 8 | 23,501 | vdp-counters-flycast.log |
| DMA fill decodes registers once | 26 | 7 | 18,900 | vdp-fill-flycast.log |
| LTO for VDP state/port and bus glue | 19 | 5 | 12,834 | vdp-lto-flycast.log |
| FM fast runs between envelope steps | 14 | 3 | 9,151 | fm-fast-flycast.log |
| Word-wise renderer cache compares (PC profiler on) | 10 | 2 | 7,373 | pcprof-eq-flycast.log |

- **Stream cushion.** KOS fills both 4,096-frame halves on start; starting at
  8,192 queued frames left ~700 frames of cushion, so ordinary jitter met refill
  requests short even in idle menus. Every later request then ran short too. The
  stream now starts with 2,048 more frames and, on a shortfall, leads with enough
  silence to restore that cushion (one gap instead of repeated ones).
- **Consumption matches production.** KOS's firmware plays 53,267 Hz as ~53,230 Hz;
  consumed frames match elapsed time, produced frames match submits × 889.
  All remaining silence corresponds to missed VBlanks.
- **Forced-blank DMA (fidelity fix).** `reset_vdp_and_graphics_state` ($7FB8)
  clears VRAM with a 64 KiB DMA fill while register 1 blanks the display, then
  spins on the DMA-busy bit (`loc_7FE4`). The pinned VDP model always used
  active-display rates, so the fill lasted ~15 frames of emulated time and each
  transition stalled for 8 paced frames. `tools/vdp_patches.py` applies Genesis
  Plus GX's blanking counts (68K 166/204, fill 165/203, copy 83/102 per H32/H40
  line) when the display is disabled at DMA start. Against Genesis Plus GX, the
  action (1,481) and two-player (761) observations and every per-region report
  are unchanged; both gates are reached at the same frames. Native transitions
  were already shorter than the original's (decompression is native), and the
  gates absorb that. Pre-gate music position and palette-fade counters differ
  from the previous native baseline, as expected from the shorter stall.
- **HV counters** were recomputed with three 64-bit divisions (software on SH-4)
  per status read. A cached frame base gives the same counters with 32-bit
  arithmetic. The DMA fill decoded the increment and SAT base per byte. Both
  changes leave replay PCM and RAM byte-identical.

- **FM fast runs.** Within a channel span, an operator's attenuation and power
  table only change at envelope steps, which fall on predictable counter
  values ((T & mask) == 0). `sor_fast_run()` finds the next sample where any
  operator needs a full clock; until then, phases, steps and power tables stay
  in locals and output follows `output_4op_fixed` exactly. SSG-EG, inverted and
  LFO-PM operators always take the full path. The gameplay window now shows
  1,659 flips over 1,660 VBlanks (was 10 missed); busiest profiled frame FM
  7.3 → 6.2 ms. Envelope-run, fast-AM and fast-output mutants fail the FM test.
  A `SLOW` line means two VBlanks passed between frame starts; the queued PVR
  frame can still absorb that, so flips versus VBlanks is the refresh metric.

- **PC profiler.** `SOR_PC_PROFILE=1` samples the interrupted PC at 10 kHz from
  TMU1 (Flycast does not raise the watchdog interval interrupt) into gameplay and
  other bins; `tools/pc-profile.py <log>` resolves them against `build/native/sor.elf`.
  Its first gameplay profile: 27% idle, 8.5% `memcmp` (the scene cache compares
  all VRAM, CRAM, VSRAM and SAT with the previous frame, and the texture cache
  compares all 2,048 tiles), ~18% FM, ~7.5% PSG, 3.5% DAC driver.
- **Cache compares.** `sor::equal_bytes` compares aligned words (falling back to
  `memcmp`); it cut that cost to 3.4% and raised gameplay idle time to 33%. The
  gameplay window now shows **1,659 flips over 1,659 VBlanks**, with the profiler
  interrupt running. Scene tests and GPU-scene validation of all 2,865 replay
  frames pass. `pace()` now inlines its per-instruction counter update.

Remaining slow intervals: one per screen transition (the ~1.2-frame DMA wait
spans one paced interrupt, plus 13–16 ms texture uploads), bulk Nemesis uploads
through the data port (frames 212, 931, 932), and title/gameplay frames where
DAC-heavy synthesis (~12 ms) plus presentation (~8–10 ms) exceed 16.7 ms.

## Release-configuration qualification and stream latency (2026-09-18)

With audio on and both profilers off (`SOR_AUDIO_PROFILE=0`): the action replay
shows 1,659 flips over 1,659 gameplay VBlanks and the two-player encounter 941
over 941 (`qualify-audio`, `qualify-two-player` logs).

The AICA stream was rebuilt for latency (`src/dreamcast/audio_kos.cpp`):

- A feeder thread polls KOS every 2 ms, so sound RAM holds a 2,048-frame double
  buffer (was 8,192). KOS skips refills under half its buffer and rounds to 512
  frames; a 1,024-frame buffer under-filled (stale replay, ring overflow).
- Game and feeder share a single-producer/single-consumer ring (the old shared
  `queued` counter was modified from both sides).
- Clock matching: Flycast's display runs at 59.81 Hz overall, and gameplay and
  menu phases drift in opposite directions against the AICA clock, so a fixed
  rate cannot match. Each 1,024-frame refill consumes up to 4 frames more or
  fewer (nearest neighbour, <0.4%) in proportion to the ring's distance from its
  3,584-frame cushion. Playback requests 53,274 Hz (AICA step 53,273.1 Hz).
- On a shortfall the stream plays silence until the cushion is restored: one gap
  per production stall. Stall deficits measured from `SLOW` lines: most screen
  transitions need ≤1,926 frames; two bulk-decompression loads need 4,108 and
  4,460. Only those two underrun, while the screen is blank (~96 ms each).
- Synthesis-to-DAC delay is now ~98 ms (ring mean ~3,670 frames plus 1,024–2,048
  in sound RAM), from ~155 ms. `AICA_UNDERRUN` lines give each underrun's frame.

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
