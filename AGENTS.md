# Development preferences

- Start emulators and other GUI applications hidden and in the background. Do not
  activate or raise their windows over the user's work. On macOS use the provided
  `tools/run-flycast.sh` launcher (`open -g -j`); a shell `&` alone is insufficient.
- Prefer headless reference tests and file/serial captures. Do not focus a window
  merely to inspect progress.
- Keep supplied ROMs, extracted game assets, generated game code, and disc images
  out of git. Commit and push reviewable source changes to `main` as requested.
