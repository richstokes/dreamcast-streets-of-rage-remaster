# Working in this repository

- Graphical runs go through `tools/run-flycast.sh` (starts muted; a background
  window is best effort). Prefer the headless build and the host tests where
  they answer the question.
- Never commit the ROM, extracted game assets, generated game code or disc
  images. Everything derived from the ROM stays under `build/` or `dist/`.

# Where things are explained

- Enhanced graphics (replacement art, its keys, per-round loading, the art
  pipeline and its memory budgets): `docs/REMASTER.md`. Read it before changing
  the renderer's sprite path, the art tools or the package format.
- Performance measurement and its pitfalls: `docs/OPTIMIZATION_LOG.md`,
  `tools/bench-flycast.sh`, `tools/flycast-speed.py`.
- Fidelity against the original: `docs/FIDELITY_GATE.md`, `docs/REFERENCE.md`.
