#!/usr/bin/env python3
import json,struct,sys
from pathlib import Path
BUTTONS={'UP':1,'DOWN':2,'LEFT':4,'RIGHT':8,'B':16,'C':32,'A':64,'START':128}
s=json.loads(Path(sys.argv[1]).read_text())['segments']
if not 1<=len(s)<=128: raise SystemExit('1..128 segments required')
b=bytearray(b'SRP1'+struct.pack('<I',len(s)))
for v in s:
 if not 1<=v['frames']<=60000:raise SystemExit('Invalid frame count')
 b+=struct.pack('<IHH',v['frames'],*(sum(BUTTONS[k] for k in v.get(p,[])) for p in ('p1','p2')))
Path(sys.argv[2]).write_bytes(b)
