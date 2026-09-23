# Input audit (2026-09-15, historical)

The audit of the two research repositories made before any code was written.
What was built from them is in ARCHITECTURE.md and REFERENCE.md.

Exact commits, including all four recursive submodules, are in
[`upstream-lock.json`](../tools/upstream-lock.json). Both requested repositories
were cloned recursively. Only the SoR1 assembly was inspected. Research checkouts
and their embedded game data remain ignored and are not redistributed by this repo.

## Findings verified in source / builds

| Question | Evidence and result |
| --- | --- |
| Is this a complete reconstructed C++ game? | No. `StreetsOfRageRecompilation/code-analysis/manual_functions.txt` lists 53 manual entry addresses; 3,373 lines across the top-level hand-written .cpp files. These counts do not measure gameplay coverage. Most gameplay depends on generated bodies. |
| What is generated? | `RageDecompiler/tools/recompiler/generator.py` emits `SoR.hpp`, `SoR-common.hpp`, grouped `SoR-XXX.cpp`, address dispatch and call/return glue. `generated/` is absent and gitignored. ROM required even for initial C++ compilation. |
| What remains unresolved? | The generator's speculative rejection pass can reject invalid opcodes; default dispatch calls `unhandledDispatch`. The auxiliary database has 1,068 address lines, not proof of full reachability. Exact emitted/rejected routines and runtime missing dispatches cannot be counted without generating and exercising the ROM. |
| What is manually reconstructed? | Main loop/VBlank waits, pad sampling, Nemesis/Enigma/Kosinski decompression, menus, interaction and attack hooks, object/sprite update wrapper, three small sound helpers. Many call back into generated functions. |
| Original assembly? | SoR1 disassembly is Motorola 68000 assembly plus embedded data, pointers, sprite mappings, load cues and compression streams, with an IDA database. It is supporting evidence, not SH-4 source. Z80 DAC code remains ROM data uploaded to a Z80 emulator. |
| Hardware dependency? | `SystemMemory.cpp` routes VDP ports, Z80 RAM/BUSREQ, YM2612, controller I/O and TMSS. `SoRSound.cpp` explicitly retains generated engine/channel ticks. `MegaDriveEnvironment` supplies VDP rasterization, YMFM, PSG, Z80 and SDL event/audio/video services. |
| Word sizes and endian? | `data_types.hpp` uses uint8/16/32_t. `CPU68K.hpp` merges subregister writes explicitly. Memory uses big-endian shifts and 24-bit normalization. No x86-specific instruction was identified in the inspected generator; this is not a full target certification. |
| SH-4 restrictions? | `SystemMemory.hpp` statically requires lock-free byte/word atomics, uses atomic_ref/wait/shared_mutex and per-byte compare/exchange; the host has separate device threads and SDL in public headers. Generated wide arithmetic uses uint64_t and software division may cost more on SH-4. Per-instruction IRQ/pacing and dual native/emulated call stacks need profiling. |
| Allocation risk? | Host allocates a full 4 MiB cartridge region for a 512 KiB game, a temporary file-size vector, WRAM atomics and dynamically growing sound event buffers. The host CMake even requests an 8 MiB Windows stack. None is a retail Dreamcast budget. |
| Compiler semantics? | Generator uses unsigned widening for carry, explicit casts for sign extension and one-bit loops for shifts. Validate emitted operations on host and SH-4; portable-looking code is not behavioral proof. |
| PC build? | CMake Release configured with AppleClang/SDL3. MegaDriveEnvironment and its dependency libraries built. Game compilation failed on missing `SoR.hpp`. `scripts/generate_cpp` failed with `ROM not found`. No playable host or gameplay runtime verification yet. |

## Data layout evidence

`SoRInteractions.cpp` uses 128-byte object slots at FF B800 (P1), FF B880
(P2), and FF B900 onward. X/Y/Z start at offsets 10/14/18 hex; state at 30;
held object pointer at 5E; combo state at 5D. Keep these as addresses/bytes,
not pointer-cast native structs. Slots include fixed-point fractions and overlapping
fields with type-specific meanings.

`SoRControls.cpp`, `SoRInteractions.cpp`, `SoRManualFunctions.cpp` and the SoR1
assembly should be consulted together. Assembly P1_SST/P2_SST/Enemy_SST labels
corroborate the object bases. ROM animation/mapping tables, tile art and palettes
feed the RAM sprite list / VDP, rather than independent PNG frames. `SoRDecompress.cpp`
contains Nemesis, Enigma and Kosinski decoding. The assembly identifies the enemy
load-cue buffer at FF6800, camera X at FFE002, scene length/progression at
FFE01A/FFE01E; these are reference candidates pending traces, not verified gameplay.

The label database contains 501 labelled entries and 258 named RAM addresses.
Its percentages and AI analyses are author annotations, not this project's tests.
Collision descriptors, enemy state tables, encounter streams, boss and ending
paths need ROM-driven extraction and comparison. Do not infer completeness from labels.

## Licensing / assets

- Recompilation: MIT, copyright Rui Nelson 2026.
- MegaDriveEnvironment: MIT, copyright Rui Carneiro 2026; embedded YMFM is BSD-3,
  SUZUKI PLAN Z80 is MIT. See its LICENSE and THIRD_PARTY_NOTICES.md.
- No root LICENSE located in the meta-project, RageDecompiler, sample submodule,
  or gsaurus disassembly. Keep them as separately fetched research; do not
  relabel their contents as this repository's MIT code. Clarify permission before
  redistributing their code or generated derivatives where necessary.
- Game ROM, artwork, music and extracted/generated derivatives are not covered
  by the new code's MIT license. Supply game data locally; no ROM download or
  reconstruction from assembly is part of the tools.
- KallistiOS has its own license; binary distribution must carry its notices.

## Audio dependency use

The native audio path uses ymfm (BSD-3-Clause, Aaron Giles) and the
Suzuki Plan Z80 core (MIT, Yoji Suzuki) bundled in the pinned MegaDriveEnvironment
revision. They are staged during preparation with original headers intact. Binary
and source distribution notices are in licenses/ymfm.txt and
licenses/suzukiplan-z80.txt. No Genesis Plus GX code is linked into the target.
The DAC program and sample bytes come only from the user's verified ROM.
`tools/audio_patches.py` now applies a checked ymfm single-channel output
specialization while retaining its copyright/license headers and chip arithmetic.
The Z80 dependency remains unchanged; native hot instructions live separately
in `src/audio/z80_hot.hpp` and are compared against its register/cycle behavior.
