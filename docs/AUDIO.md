# Original audio development checkpoint

Current audio update: [native DAC decoding and AICA evaluation](NATIVE_DAC.md).
The playback loop now has a validated native path; interpreter setup/fallback remains.

**Enabled by default on Dreamcast (2026-09-19).** The original sound sequencer,
FM, PSG and the ROM's sampled drum/voice driver run at 60 Hz in Flycast: the
action replay shows 1,659 flips over 1,659 gameplay VBlanks and the two-player
encounter 941 over 941, with audio on and profiling off. Stream underruns occur
only during the two bulk-decompression screen loads (display blanked). Timing is
verified against the original ROM in Genesis Plus GX (below). Physical-console
performance, full soundtrack/SFX coverage and listening QA remain unverified.
`SOR_AUDIO=0` builds the silent configuration.

## Try it

```sh
./build-and-run.sh
```

This rebuilds and launches the embedded-ROM ELF with original audio.
`SOR_AUDIO=0 ./build-and-run.sh` rebuilds the silent configuration.
The flag is recorded in a generated header tracked by Make dependencies, so
switching configurations does not require a clean build. GUI launch is best-effort
background as usual. The sound option itself is **not** a gameplay speed control.

For a reproducible disc replay:

```sh
SOR_REPLAY="$PWD/reference/scenarios/phase-aligned-actions.json" \
  ./tools/package.sh "$SOR_ROM"
./tools/run-flycast.sh dist/sor.cdi
```

## Architecture and provenance

- Generated/manual SoR sound routines remain the original sequencer. MMIO routes
  YM2612 and PSG writes into `src/audio/audio_core.cpp` instead of discarding them.
- ymfm (Aaron Giles, BSD-3-Clause) comes from the pinned MegaDriveEnvironment
  input. The small sound-only Z80 interpreter is Suzuki Plan (MIT), from that same
  pin. Headers retain attribution; full notices are under `licenses/`.
- The supplied ROM's decompressed DAC program runs against 8 KiB local Z80 RAM,
  a bounded banked ROM view and sound registers. No Genesis/68000 emulator is
  linked. No new music or downloaded game data is required.
- One game frame advances 896,040 master clocks. Each YM output sample advances
  1,008 master clocks; integer remainder carries between frames. The resulting
  source rate is 53,267 Hz. FM timers and DAC execution follow this simulated
  timeline, not wall-clock threads. PSG tone/noise is integrated across samples.
- A single-owner KOS backend polls a stereo PCM16 AICA stream. Its main-RAM ring
  is 64 KiB, callback buffer 16 KiB, and explicit sound-RAM stream is 32 KiB,
  separate from the AICA driver and KOS staging buffers. Allocation failures and
  invalid bank reads are reported. No desktop paths are used on the console.
  The compiler removes static PCM buffers when audio is disabled, and the
  synthesizer and AICA stream are not initialized. The optimized default ELF has 2,504,864
  text, 5,848 data and 1,599,192 BSS bytes. Audio-enabled sampled heap use is
  2,379,384 bytes; free VRAM is 3,136,104 bytes, and free AICA memory after stream
  initialization is 1,867,776 bytes. These are samples, not proven peak budgets.
- The pinned KOS implementation passes **byte counts** to its stream callback;
  the header's legacy parameter names say samples. Stereo frames are four bytes.

## Reproduce host captures

```sh
./tools/build-headless.sh
python3 tools/native-reference.py "$SOR_ROM" \
  reference/scenarios/phase-aligned-actions.json build/native-audio --audio-wav
build/tools-venv/bin/python3 tools/genesis_reference.py \
  research/Genesis-Plus-GX/genesis_plus_gx_libretro.dylib "$SOR_ROM" \
  reference/scenarios/phase-aligned-actions.json build/original-audio --raw-ram --audio-wav
python3 tools/compare-phase.py build/original-audio build/native-audio \
  --segment 9 --require-observations-equal
```

Captures are local derivatives of the supplied ROM and remain ignored. Original
reference WAV rate comes from libretro AV timing; native WAV rate comes from the
sound core. Native headless runs enable synthesis by default; set `SOR_AUDIO=0`
to reproduce the earlier silent WRAM traces. `--audio-wav` explicitly enables it.

## Sound timing against the original ROM

`reference/results/audio-timing-2026-09-19.json` compares the action replay with
Genesis Plus GX (MAME YM2612 core) after replay gate 9:

- **68000 driver state** (`tools/compare-sound-state.py`): all four effect channels
  match on all 1,300 compared frames at the gameplay alignment. Music started 47
  frames earlier relative to gameplay in the native run (screen transitions differ;
  see the cadence item in TODO.md); aligned at its own start, all nine music
  channels match except channel flag bits that effects also write.
- **Audio** (`tools/compare-audio.sh`, music-aligned): note/effect onsets align
  within one 10 ms analysis hop in every window; log-band spectral correlation
  0.963 (median). Native output is a uniform ~3.4 dB quieter below 2 kHz and has
  less high-frequency rolloff; that is the cores' gain/filter convention, not
  missing content.
- **68000 write timing.** The driver keys each channel off and on again for a new
  note. Applying all 68000 writes at one sample hid those key-offs from ymfm, so
  sustained parts never retriggered and faded (bass 20 dB low). 68000 writes are
  now placed by emulated time within the frame, and never split the Z80's
  address/data pair. `tools/ym-render.cpp` replays a host `SOR_YM_LOG` write log
  through ymfm or Nuked OPN2 for such investigations.

## Playback stream

A 2 ms feeder thread fills a 2,048-frame AICA double buffer from a
single-producer/single-consumer ring with a 3,584-frame cushion. Refills consume
up to 0.4% more or fewer frames to follow the display/AICA clock ratio. Delay
from synthesis to output is about 98 ms. Details: OPTIMIZATION_LOG.md.

## Verified so far

- Sanitized tests cover disabled mode, deterministic stereo, rational sample
  counts, FM/PSG output, DAC panning and Z80 banked reads/BUSREQ handling.
- The 2,865-frame action replay produces 2,546,780 stereo sample frames. Two host
  captures and their full game-RAM traces are identical. Peak magnitude 13,338;
  no clipped samples in this capture. This is signal validation, not listening QA.
- All 1,481 phase-aligned gameplay observations still match the original ROM with
  audio active. Disabling audio retains the previous 2,159-snapshot native trace.
- Rebuilt default configuration: 1,200 gameplay flips across 1,200 VBlanks in
  Flycast, with a 16.725 ms mean CPU-loop interval. Retail cadence is unverified.
- SH-4 PCM checkpoints at frames 599/1199/1799/2399 match host FNV-1a hashes:
  `be87a0ed`, `d4cadb81`, `c83a1041`, `d8066ab0`. All four contain 889 stereo frames.
  This establishes cross-platform synthesis at those checkpoints, not equality
  with the original console's complete waveform or with AICA output after streaming.

## Performance history

The figures in this section predate the 2026-09 optimization work
(OPTIMIZATION_LOG.md) and describe why audio was once opt-in.

The optimization pass in AUDIO_OPTIMIZATION.md lowers the first 1,200 gameplay
intervals from 39.220 to 29.218 ms mean (25.5%). p95 falls from 47.0 to 37.5 ms.
It preserves the full host PCM/RAM replay and four SH-4 PCM checkpoints. Audio
still underruns and remains opt-in. The following figures describe the earlier
prototype baseline; current counters are in
`reference/results/audio-optimized-2026-09-15.json`.

The current interpreter/synthesizer is too expensive for retail-budget 60 Hz.
The measured Flycast audio-enabled checkpoint took about 38–40 ms per gameplay
loop and missed refreshes. Sampled synthesis work was roughly 29–37 ms; profiling
splits show about 16 ms in DAC-driver execution, 9–17 ms in FM, and 4 ms in PSG.
The profiling clock calls add overhead. Runtime `stream_us` at the diagnostic
frame includes serial logging and must not be treated as typical transfer cost.
Detailed counters and capture hashes are in
`reference/results/audio-2026-09-15.json`.

A small initial stream buffer wrapped between slow polls; increasing its size
made the supply deficit visible as callback underruns. A larger main-RAM ring
removes initial prefill truncation but cannot fix sustained starvation. Counts
are diagnostics, not a claim of glitch-free playback. Default builds therefore
retain the measured silent rendering checkpoint.

Next: replace the DAC interpreter's costly delay/poll work with a verified native
decoder or equivalent fast paths; reduce FM/PSG synthesis costs; then reduce
buffer latency and verify long-run clock drift, underruns and loaded frame times.
Preserve this deterministic core as an audio reference for each optimization.
68K chip writes are currently applied at frame boundaries; subframe register
scheduling, PSG edge details, busy/timer behavior, mix/filter balance and audible
fidelity against original captures still require validation. Full soundtrack/SFX,
all-stage and physical-hardware coverage remain open.
