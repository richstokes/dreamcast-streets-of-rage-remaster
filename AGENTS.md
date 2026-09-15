# Development preferences

- Make a best effort to launch emulator and test windows in the background and
  avoid interrupting the user's desktop. Flycast may bring itself forward even
  with `open -g -j`; this is acceptable when keeping it behind other windows is
  not straightforward. Do not block progress or spend excessive time on focus handling.
- Graphical Flycast runs are authorized for development, visual verification,
  and screenshots. Do not disable graphical testing or require additional user
  permission for each run. Prefer background windows, but continue graphical
  testing and screenshots even when background operation is unreliable.
- Use `tools/run-flycast.sh` for graphical runs. Its `open -g -j` flags are best
  effort, not a prerequisite for testing. Preserve graphical rendering and
  screenshot capability.
- Prefer headless reference tests and file/serial captures where practical.
  Avoid unnecessary window activation, but allow it when needed for verification.
- Keep supplied ROMs, extracted game assets, generated game code, and disc images
  out of git. Commit and push reviewable source changes to `main` as requested.
