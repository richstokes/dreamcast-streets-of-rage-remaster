#!/usr/bin/env python3
"""Record the rebuilt PC implementation using frame-boundary lockstep and raw RAM."""
import argparse,json,sys
from pathlib import Path
from genesis_reference import observation
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'research/StreetsOfRageProject/MegaDriveEnvironment/python/src'))
from megadrive_remote import MegaDriveClient,Buttons

def main():
 p=argparse.ArgumentParser();p.add_argument('scenario');p.add_argument('output',type=Path);p.add_argument('--port',type=int,default=7777);a=p.parse_args()
 a.output.mkdir(parents=True,exist_ok=True);scenario=json.loads(Path(a.scenario).read_text())
 with MegaDriveClient('127.0.0.1',a.port) as c, (a.output/'trace.jsonl').open('w') as trace:
  c.restart_game();c.set_lockstep(True)
  initial=c.get_game_uptime_frames();expected=0
  if initial>30:raise RuntimeError(f'Reset lockstep started too late: {initial}')
  for index,segment in enumerate(scenario['segments']):
   masks=[sum(int(getattr(Buttons,b)) for b in segment.get(k,[])) for k in ('p1','p2')]
   count=segment['frames']-(initial if index==0 else 0)
   s=c.step_input(player1=masks[0],player2=masks[1],held_frames=count,total_frames=count,timeout_ms=30000)
   trace.write(json.dumps(observation(s.work_ram,s.frame))+'\n')
   if 'capture' in segment:
    c.read_framebuffer().save_ppm(str(a.output/(segment['capture']+'.ppm')))
  c.set_lockstep(False)
 (a.output/'metadata.json').write_text(json.dumps(dict(initial_frame=initial,source='patched-entry-list upstream PC, silent'),indent=2)+'\n')
if __name__=='__main__':main()
