#!/usr/bin/env python3
"""Run the Dreamcast simulation headlessly, retaining every WRAM snapshot locally."""
import argparse
import hashlib
import json
import re
import os
import wave
from pathlib import Path
import subprocess
import sys
from genesis_reference import observation
from rom import inspect

ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom',type=Path);ap.add_argument('scenario',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--audio-wav',action='store_true',help='Capture experimental original audio to audio.wav')
    args=ap.parse_args();identity=inspect(args.rom.read_bytes())
    if identity['sha256']!='dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0':
        ap.error('ROM does not match the generated-code target SHA-256')
    args.output.mkdir(parents=True,exist_ok=True)
    replay=args.output/'replay.bin';raw=args.output/'ram.bin'
    subprocess.run([sys.executable,str(ROOT/'tools/replay.py'),str(args.scenario),str(replay)],check=True)
    run_env=os.environ.copy()
    audio_path=args.output/'audio.s16'
    if args.audio_wav:run_env.update(SOR_AUDIO='1',SOR_AUDIO_CAPTURE=str(audio_path.resolve()))
    with (args.output/'run.log').open('w') as log:
        subprocess.run([str(ROOT/'build/headless/sor-headless'),str(args.rom.resolve()),str(replay),str(raw)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300,env=run_env)
    if args.audio_wav:
        with wave.open(str(args.output/'audio.wav'),'wb') as audio:
            audio.setnchannels(2);audio.setsampwidth(2);audio.setframerate(53693175//7//144)
            with audio_path.open('rb') as pcm:
                while block:=pcm.read(65536):audio.writeframesraw(block)
    events=[dict(segment=int(i),frame=int(f),idle_frames=int(n)) for i,f,n in re.findall(r'REPLAY gate (\d+) matched at frame (\d+) after (\d+) idle frames',(args.output/'run.log').read_text())]
    (args.output/'events.json').write_text(json.dumps(events,indent=2)+'\n')
    # Capture k is the state before VBlank k+1; the reference harness calls
    # that frame k+1 (its first frame, power-on to the first VBlank, is frame 1).
    frames=1
    with raw.open('rb') as source,(args.output/'trace.jsonl').open('w') as target:
        while True:
            ram=source.read(65536)
            if not ram:break
            if len(ram)!=65536:raise ValueError('Truncated trace')
            target.write(json.dumps(observation(ram,frames))+'\n');frames+=1
    (args.output/'metadata.json').write_text(json.dumps(dict(rom=identity,frames=frames-1,ram_first_frame=1,audio_enabled=run_env.get('SOR_AUDIO','1')!='0',
        native_dac=run_env.get('SOR_DAC_NATIVE','1')!='0',aica_split=run_env.get('SOR_DAC_AICA','0')=='1',
        scenario_sha256=hashlib.sha256(args.scenario.read_bytes()).hexdigest(),
        backend='shared Dreamcast simulation; host offscreen platform',
        sampling='Before VBlank presentation and next input; capture is preceding presentation'),indent=2)+'\n')
if __name__=='__main__':main()
