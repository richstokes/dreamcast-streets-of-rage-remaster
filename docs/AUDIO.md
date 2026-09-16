# Original audio development checkpoint

Current audio update: [native DAC decoding and AICA evaluation](NATIVE_DAC.md).
The playback loop now has a validated native path; interpreter setup/fallback remains.

**Experimental, disabled by default on Dreamcast.** The audio path is functional,
including the original sound sequencer, FM, PSG, and the ROM's sampled drum/voice
driver. It currently misses frame deadlines and starves the AICA stream. It is
not a completed or fidelity-certified original-audio implementation.

## Try it

```sh
SOR_AUDIO=1 ./build-and-run.sh
```

This rebuilds and launches the embedded-ROM ELF with experimental audio enabled.
Use plain `./build-and-run.sh` to rebuild the normal 60 Hz, silent checkpoint.
The flag is recorded in a generated header tracked by Make dependencies, so
switching configurations does not require a clean build. GUI launch is best-effort
background as usual. The sound option itself is **not** a gameplay speed control.

For a reproducible disc replay:

```sh
SOR_AUDIO=1 SOR_REPLAY="$PWD/reference/scenarios/phase-aligned-actions.json" \
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

## Performance and next work

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
