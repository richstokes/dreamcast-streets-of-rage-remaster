# Delivery tracker

The project is an original-mode checkpoint, not a complete port or remaster.
Evidence and measurements live in PROGRESS.md and REFERENCE.md.

## Next: establish the faithful first section

- [x] Pin/audit both inputs, verify supplied JUE rev00 ROM, cross-build and boot.
- [x] Native PowerVR path and reproducible two-player Flycast performance replay.
- [x] Preserve deterministic simulation and compare explicit update phases.
- [x] Compare sampled directions, jump actions, special and two-player encounter.
- [ ] Explain remaining startup timers/flags and active-object tail differences.
- [ ] Exercise grabs, throws, combos, recovery, invulnerability and enemy families.
- [x] Build and profile an experimental original FM/PSG/DAC-to-AICA path.
- [x] Reduce audio CPU overhead with bit-exact native hot paths and FM output specialization.
- [ ] Replace remaining DAC driver cost; evaluate native decoding and AICA sample playback.
- [ ] Finish FM/audio optimization to retain 60 Hz, remove starvation and validate audible fidelity.
- [ ] Wire settings/records to VMU saves; test writes, corruption and missing cards.

## Complete original mode

- [ ] All eight rounds, bosses, transitions, both endings and two-player progression.
- [ ] Stage/effect renderer coverage, loading stalls and audio underruns.
- [ ] Main RAM/stack, VRAM and sound RAM high-water measurements.
- [ ] Repeated stage loads and uninterrupted full playthrough.
- [ ] Human retail tests from HARDWARE_TESTS.md (do not block emulator work).

## Enhanced presentation, after the first-section fidelity gate

- [ ] Source-art/import/atlas pipeline with frame dimensions, pivots and bounds checks.
- [ ] Complete redrawn character animations with unchanged gameplay anchors/hitboxes.
- [ ] Stage-specific redrawn environments and appropriate parallax/animation.
- [ ] PowerVR-friendly effects and presentation-only animation polish.
- [ ] HUD/menus, streaming replacement-music capability, original soundtrack option.
- [ ] Selectable original/enhanced modes and comparison captures.
- [ ] Full enhanced playthrough and repeatable retail-budget performance validation.
