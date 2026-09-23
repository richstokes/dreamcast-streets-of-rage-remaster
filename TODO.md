# Roadmap and known limitations

What is done is in [README.md](README.md) ("Status") and the dated entries of
[docs/PROGRESS.md](docs/PROGRESS.md). The project started from the brief in
[docs/BRIEF.md](docs/BRIEF.md).

## Fidelity

- Rounds 2–8 run to the ending but have not been compared with the original;
  Round 1 has ([docs/FIDELITY_GATE.md](docs/FIDELITY_GATE.md)).
- Exact Z80/68000 bus interleaving for the sound driver's acquire loop: the
  port's Z80 drifts a few hundred clocks from the original's and one drum
  command is skipped in the two-player window (original frame 5,925).
- Remaining timing: exact costs for the profile-costed hand-written routines,
  DIV and register-shift timing, YM2612 busy from the Z80's writes
  ([docs/CADENCE.md](docs/CADENCE.md)).
- Main RAM, stack, VRAM and sound-RAM peaks have not been measured through a
  whole playthrough; the figures on record are samples.

## Hardware

- Not yet run on a real Dreamcast: performance, AICA output, VGA and TV output,
  GDEMU and CD-R boot ([docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)).

## Enhanced graphics

- The art is generated from the original frames; hand-drawn replacements are
  not started (the pipeline accepts them: `tools/make-enhanced-art.py --override`).
  Hand-made art is checked for size only, not for its anchor or coverage.
- Smooth animation has few in-betweens: the generator accepts 20 of 975 pose
  pairs ([docs/REMASTER.md](docs/REMASTER.md)).
- Backgrounds, the HUD and menus keep the original art.
- No replacement music or streamed soundtrack.

## Persistence

- Settings (the options menu) last for the session; nothing is saved to a VMU.
