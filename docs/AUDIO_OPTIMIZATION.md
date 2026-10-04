# Audio optimization and hardware split (2026-09-15)

## Decision

Keep the original sound sequencer on SH-4. Use AICA for sample playback, playback
rate, panning/mixing and streamed output. Replace expensive sound-driver work
incrementally, using the first deterministic audio implementation at `a73acfc`
as the regression reference. Do not change the game cadence to hide audio cost.

AICA's KOS channel formats are PCM8, PCM16 and Yamaha ADPCM; they are not a
YM2612 FM register interface ([KOS sample formats](https://kos-docs.dreamcast.wiki/group__audio__aica__samples.html)).
The supplied game's DAC compression is also not AICA's ADPCM format. Sending
its compressed bytes directly to a hardware channel would produce wrong sound.

The pinned KOS `snd_stream.c` already splits stereo and chains
`spu_dma_transfer` calls to AICA RAM. The current port uses that path. Moving the
same transfers into handwritten assembly would duplicate working hardware access.
The sampled synthesis cost, rather than that DMA path, is the principal problem.
Do not mistake the diagnostic frame's serial-print cost in `stream_us` for normal
DMA cost.

| Work | Current implementation / decision | Gate before further migration |
| --- | --- | --- |
| Original sequencer and sound priorities | Native translated SoR code on SH-4 | Preserve command order, music/SFX channel stealing and gameplay-visible busy state |
| DAC driver polling/delays | Native hot instructions, batched interpreter fallback | Compare flags, refresh register, instruction-boundary overshoot and every PCM sample |
| Drum/voice decoding and playback | Remaining driver still runs in sound-only interpreter | Next major candidate: native decoder and bounded PCM cache, then AICA playback; verify pitch, sample boundaries, interruption and DAC mixing first |
| FM operators | Optimized ymfm on SH-4 | No direct AICA FM equivalent exposed by KOS. Keep live effects/priority semantics; independently benchmark any ARM/DSP implementation rather than assuming offload is faster |
| PSG | Cached level with exact divider/edge integration | AICA looped waveforms are possible, but pitch quantization and phase on writes must be compared; noise needs its own faithful path |
| Music assets | Live original synthesis remains the correctness reference | Offline-rendered voice stems / streamed music can remove major cost, but require verified track loops, channel stealing, pause and transitions; one mixed music file is insufficient evidence |
| Final stream | KOS DMA plus AICA playback | Continue measuring underruns, queue drift and latency; larger queues cannot fix sustained CPU overload |

No ARM firmware replacement or new SH-4 assembly is justified by this pass.
Retain KOS's audio firmware and platform services. Hardware offload remains an
implementation objective, not a claim that FM synthesis has already moved.

## Implemented changes

- Incremental exact-ratio Z80/YM clocks replace repeated wide multiplication and
  division. Reset/BUSREQ behavior retains the previous timeline.
- PSG amplitude is updated when polarity or volume changes, instead of adding
  four channel levels at every divider span. Edge integration is unchanged.
- Native LD A,(HL), CP, conditional JP and self-DJNZ paths remove hot interpreter
  dispatch. They only read local RAM, with interrupts and wait states disabled.
  All other instructions retain batched execution. Running that fallback one
  instruction at a time was measured to regress active drums and was removed.
- ymfm has a direct single-channel output path for YM2612 DAC-discontinuity
  mixing. It avoids scanning six channels per request and retains all operator
  arithmetic, channel order, active masks and rhythm/debug fallbacks. The
  reproducible staging patch checks its pinned source shape and retains notices.
- Only the audio units use `-O3 -flto`, allowing optimization across the synth and
  callback boundary. No fast-math or change to sample rate is used.

## Validation

`tools/test-host.sh z80-hot` uses ASan/UBSan and compares complete CPU register state
and consumed clocks against the pinned interpreter: all 65,536 CP operand pairs,
all 256 delay counts across 150 deadlines, polling boundaries, refresh wrapping,
RAM limits, MMIO fallback, interrupts and wait states.

The entire 2,865-frame action replay retains identical PCM and 2,866 identical
RAM snapshots. PCM SHA-256:
`159b3b17df95ed377fd2eedb2ff20df0c34cab1f61f5a7c2877f17c4b3d21a9d`.
This is equivalence to the prototype, not certification of its unverified PSG,
subframe scheduling or analog mixing fidelity against original hardware.

`tools/test-host.sh ymfm-output` also compares synthetic stereo samples against
unmodified pinned ymfm across randomized FM algorithms, envelopes, panning,
key-ons, LFO and DAC writes. Both sides run with ASan/UBSan.

First 1,200 gameplay intervals: mean loop time falls from 39.220 to 29.218 ms
(25.5%); p95 from 47.0 to 37.5 ms. This still misses 60 Hz and underruns.

Final measured counters are recorded in `reference/results/audio-optimized-2026-09-15.json`.
Audio has been on by default since the later work in OPTIMIZATION_LOG.md
(2026-09-19). Physical hardware measurements began on 2026-10-04; see
OPTIMIZATION_LOG.md for the release-build comparison and remaining underruns.

GCC 15 LTO emits a bounds warning in upstream `opn_registers_base::write` about
index 512. The YM2612 address setters accept a byte or `0x100 | byte`, so this
path is bounded to 0–511; the upstream bounds assertion remains enabled. No
out-of-bounds access was observed in sanitized synthetic-register tests. The
warning is retained, not suppressed; reassess it if address/state loading changes.

## Candidate: batch FM across DAC data writes (2026-10-04)

**Not implemented.** First measure the smaller stream-copy and FM-table changes
on hardware. If synthesis still misses deadlines, this is a candidate for a
separate, switchable experiment.

The physical-console action-replay profile at
`build/hardware/20261004T162416.222361Z/console.log` records frame 2399 at
17.016 ms synthesis: FM 13.257 ms, PSG/mixing 2.329 ms, Z80 1.301 ms. Its workload
has 14 audible operators, with neither SSG-EG nor dynamic phase operators. These
are instrumented samples, not release-performance qualification; the serial
capture stopped before the gameplay PC histogram was transmitted.

Between frames 1799 and 2399, the log records 134,087 DAC writes, about 223 per
frame. `NativeAudio::renderBlock()` currently ends an FM span at every data
write, including each DAC sample. This suggests spans averaging roughly four
output samples during continuous drums; measure a host span-length histogram
before implementing. Repeated channel/operator setup for these short spans may
cost more than advancing the FM waveform itself.

The smallest proposed change is an optional raw DAC-data timeline passed to
`ym2612::sor_generate_span()`:

- Scan the already-merged events in their existing order. Address-latch writes
  and DAC data writes to registers 0x2A/0x2C can populate the timeline without
  splitting FM synthesis. Classify data writes using the actual chip latch,
  including its bank; apply each event exactly once.
- Keep boundaries for every other data write, particularly DAC enable (0x2B),
  panning and FM registers. Keep the existing timer/CSM and unknown-driver
  fallback. Preserve same-sample event order and sample positions.
- Store the full nine-bit DAC value, including the 0x2C low bit. Mix each
  timeline value into the raw FM sum **before** the existing integer scaling
  and division. Adding independently rounded PCM components afterward is not
  equivalent.
- The periodic prepare fallback calls `generate(1)`, which reads the chip's
  stored DAC value. Substitute that sample's timeline value for this call and
  restore the final latched value afterward. The optional split-DAC backend
  also needs each sample's correct component, rather than one value per span.

Use a per-instance host switch and corresponding Dreamcast build option for
an unchanged baseline and candidate. Extend the pinned-ymfm comparison with
rapid 0x2A/0x2C changes, all pans, enable transitions, same-sample writes and
spans crossing the 4096-sample prepare boundary. Compare both switches in the
DAC integration test, including PCM, sound RAM and stem sums. Require complete
action and two-player replay PCM/RAM equality, then hardware A/B runs with the
same profiling and presentation settings. Keep the change only if release
frame deadlines and underruns improve.
