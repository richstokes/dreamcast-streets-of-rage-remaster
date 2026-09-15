#!/usr/bin/env python3
"""Run the Dreamcast simulation headlessly, retaining every WRAM snapshot locally."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
from genesis_reference import observation
from rom import inspect

ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom',type=Path);ap.add_argument('scenario',type=Path);ap.add_argument('output',type=Path)
    args=ap.parse_args();identity=inspect(args.rom.read_bytes())
    if identity['sha256']!='dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0':
        ap.error('ROM does not match the generated-code target SHA-256')
    args.output.mkdir(parents=True,exist_ok=True)
    replay=args.output/'replay.bin';raw=args.output/'ram.bin'
    subprocess.run([sys.executable,str(ROOT/'tools/replay.py'),str(args.scenario),str(replay)],check=True)
    with (args.output/'run.log').open('w') as log:
        subprocess.run([str(ROOT/'build/headless/sor-headless'),str(args.rom.resolve()),str(replay),str(raw)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    events=[dict(segment=int(i),frame=int(f),idle_frames=int(n)) for i,f,n in re.findall(r'REPLAY gate (\d+) matched at frame (\d+) after (\d+) idle frames',(args.output/'run.log').read_text())]
    (args.output/'events.json').write_text(json.dumps(events,indent=2)+'\n')
    frames=0
    with raw.open('rb') as source,(args.output/'trace.jsonl').open('w') as target:
        while True:
            ram=source.read(65536)
            if not ram:break
            if len(ram)!=65536:raise ValueError('Truncated trace')
            target.write(json.dumps(observation(ram,frames))+'\n');frames+=1
    (args.output/'metadata.json').write_text(json.dumps(dict(rom=identity,frames=frames-1,ram_first_frame=0,
        scenario_sha256=hashlib.sha256(args.scenario.read_bytes()).hexdigest(),
        backend='shared Dreamcast simulation; host offscreen platform',
        sampling='Before VBlank presentation and next input; capture is preceding presentation'),indent=2)+'\n')
if __name__=='__main__':main()
