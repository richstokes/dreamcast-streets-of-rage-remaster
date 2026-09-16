# Native DAC decoding and AICA evaluation

The locked SoR1 drum/voice playback loop now runs as a native C++ DPCM state
machine. Command/header setup still uses the small sound-only Z80 interpreter;
unknown driver code falls back to it. Gameplay and its sound sequencer remain
unchanged. No custom ARM firmware or assembly is required.

## Use

```sh
SOR_AUDIO=1 ./build-and-run.sh                       # Native DAC, combined stream
SOR_AUDIO=1 SOR_DAC_AICA=1 ./build-and-run.sh        # Four AICA stem channels
SOR_AUDIO=1 SOR_DAC_NATIVE=0 ./build-and-run.sh      # Interpreter comparison
./build-and-run.sh                                # Default silent checkpoint
```

Audio is still experimental and underruns. These are comparison configurations,
not release-ready audio modes. The separate AICA path stays opt-in because the
combined stream measured faster.

## Implementation

`src/audio/dac_driver.cpp` replaces playback at driver PCs 0xD9–0x19A. Code identity
is checked before entering the native path. Nibble deltas, byte wrap, zero-nibble
repeats, descriptor delays, command interruption, busy flags and instruction-boundary
overshoot are preserved. Private playback registers need not be retained because
command setup reinitializes them; externally visible RAM, writes and timing are
compared. The original interpreter remains available for diagnosis.

The optional hardware path separates the DAC component from FM/PSG into two stereo
stems. Their integer PCM sum exactly equals the combined source PCM, including
clamping edge cases. Four stock-KOS AICA voices use one synchronized start mask,
fixed stereo pans and blocking aligned DMA into bounded circular buffers. Allocation
rejects channels above 31 because the stock KOS synchronized-start mask is 32 bits. Source
PCM equality does not certify AICA resampling, final mixing or analog output.
Sequential position reads are not a simultaneous channel-skew measurement.

AICA buffers use 64 KiB, versus 32 KiB for the combined stream. The optional path
has 160 KiB of main-RAM ring/staging buffers. Runtime audio frame buffers total
about 7 KiB of stack; stack high-water remains unmeasured. Prefill is 8,192 frames
at 53,267 Hz (~154 ms), still too much latency for finished audio. A poll delayed
by a full ring duration stops and reprimes playback, counted as a resync.

## Verification and measured decision

- Sanitized driver tests compare every sample, cycle boundary and all 8 KiB of
  sound RAM: 16 nonempty commands, their interrupted versions, and a synthetic
  zero-repeat/zero-delay wrap case (33 cases). Command 0x85 is empty.
- A 1,000-frame integration test covers reset, BUSREQ, changing commands, panning
  and DAC low-bit behavior. Native/interpreted PCM and sound RAM match; stem sums
  match the combined stream.
- Full action replay retains 2,546,780 stereo PCM frames and 2,866 gameplay RAM
  snapshots byte-for-byte against the earlier capture. Four SH-4 PCM hashes match.
- Audio, FM specialization, replay, arithmetic/save and idle-loop tests pass.

First 1,200 gameplay intervals in Flycast, using the same phase-aligned replay:

| Configuration | Mean loop | p95 upper bound | VBlanks / flips |
| --- | ---: | ---: | ---: |
| Previous optimized interpreter (`5f186a8`) | 29.218 ms | 37.5 ms | See previous results |
| Native DAC, combined stream | 23.393 ms | 31.0 ms | 1679 / 1201 |
| Native DAC, four AICA channels | 24.022 ms | 31.5 ms | 1724 / 1201 |

The combined path reduces mean time by 19.9%. The hardware path's measured copy/DMA
cost through frame 2399 averages 355 us, max 2,918 us. Both still miss 60 Hz and
underrun. Underrun counters represent different refill units across the two
backends and cannot be compared directly. No overruns or resyncs were observed
in the measured hardware run. FM synthesis is now the largest sampled audio CPU
cost; moving DAC playback alone does not solve it.

Four-channel run sampled heap use is 2,363,144 bytes, free PVR memory 3,136,104
bytes and free AICA memory after initialization 1,835,008 bytes. These are samples,
not certified peak budgets. Retail timing, listening fidelity and full-game
coverage remain unverified. See `reference/results/native-dac-2026-09-15.json`.

## Reproduce correctness checks

```sh
./tools/test-dac-driver.sh
./tools/test-dac-integration.sh
./tools/test-z80-hot.sh
./tools/test-audio.sh
./tools/test-ymfm-output.sh
./tools/test-replay.sh
./tools/test.sh
```

Run the build once to stage the pinned upstream audio dependencies before these
standalone tests. The driver tests extract a bounded local reference from the supplied, hash-checked
ROM using `tools/extract-dac-reference.py`. No ROM bytes or extracted sample data
are committed. Runtime loading still uses the game's existing native loader.
