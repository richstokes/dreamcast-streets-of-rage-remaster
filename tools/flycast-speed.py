#!/usr/bin/env python3
"""Follow build/logs/flycast.log during an interactive run and report speed.

Interactive builds log a HEARTBEAT every 600 emulated frames (about 10 s of
Dreamcast time). This prints, per heartbeat:
  host   Flycast's speed: emulated time / wall-clock time (1.00 = real time)
  guest  refresh on the Dreamcast: flips / VBlanks (1.00 = no missed frames)
A slow host with guest 1.00 means the Mac is not keeping up with Flycast; a
guest below 1.00 means the port itself misses frames.

  tools/flycast-speed.py [LOG]
"""
import re, sys, time
from pathlib import Path

log = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'build/logs/flycast.log')
FRAME_S = 600 / 59.94
pattern = re.compile(r'HEARTBEAT frame=(\d+) vblanks=(\d+) flips=(\d+) enhanced=(\d) mode=(\w+)')
position, last = 0, None
while True:
    if log.exists() and log.stat().st_size < position:
        position, last = 0, None            # a new run truncated the log
    if log.exists():
        with log.open('rb') as f:
            f.seek(position); data = f.read(); position = f.tell()
        now = time.monotonic()
        for line in data.decode('latin-1').splitlines():
            m = pattern.search(line)
            if not m:
                continue
            frame, vblanks, flips, enhanced, mode = int(m[1]), int(m[2]), int(m[3]), m[4], m[5]
            # Lines read in one batch (the log's backlog) have no arrival time.
            host = f'{FRAME_S / (now - last):.2f}' if last is not None and now - last > 1 else '  - '
            print(f'frame {frame:6d}  host {host}  guest {flips / max(1, vblanks):.2f}  '
                  f'{"enhanced" if enhanced == "1" else "original"}  mode {mode}', flush=True)
            last = now
    time.sleep(0.5)
