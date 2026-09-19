#!/usr/bin/env python3
import json,struct,sys
from pathlib import Path
BUTTONS={'UP':1,'DOWN':2,'LEFT':4,'RIGHT':8,'B':16,'C':32,'A':64,'START':128,
         'CHEATS':256,'BACK':512} # Native menu only: L+R and Dreamcast B.
s=json.loads(Path(sys.argv[1]).read_text())['segments']
if not 1<=len(s)<=4096: raise SystemExit('1..4096 segments required')
version=2 if any('wait' in v for v in s) else 1
b=bytearray(b'SRP'+str(version).encode()+struct.pack('<I',len(s)))
for v in s:
 if not 1<=v['frames']<=60000:raise SystemExit('Invalid frame count')
 b+=struct.pack('<IHH',v['frames'],*(sum(BUTTONS[k] for k in v.get(p,[])) for p in ('p1','p2')))
 if version==2:
  gate=v.get('wait',{});address=gate.get('address',0);mask=gate.get('mask',255);value=gate.get('value',0)
  if not (0<=address<65536 and 0<=mask<=255 and 0<=value<=255 and value&mask==value):raise SystemExit('Invalid byte gate')
  if gate and (v.get('p1') or v.get('p2')):raise SystemExit('State gates require released input')
  b+=struct.pack('<HBBI',address,mask,value,int(bool(gate)))
Path(sys.argv[2]).write_bytes(b)
