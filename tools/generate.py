#!/usr/bin/env python3
"""Generate SoR with disassembly-checked entry points, retaining strict opcode errors."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from rom import inspect
ROOT=Path(__file__).resolve().parents[1]
P=ROOT/'research/StreetsOfRageProject'

def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('rom',type=Path)
    a=ap.parse_args(); identity=inspect(a.rom.read_bytes())
    if identity['sha256']!='dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0':
        ap.error('This address repair was validated only against the locked ROM SHA-256')
    sys.path.insert(0,str(P/'RageDecompiler'))
    from tools.disassembler.rom import ROM
    from tools.recompiler.main import _disassemble_to_fixpoint,_load_aux,main as recompile
    assembly=ROOT/'research/sor-disassemblies/Assemblies/Streets of Rage (JUE) (REV 00) [!].asm'
    seeds={int(m,16) for m in re.findall(r'^(?:sub|loc|locret)_([0-9A-F]+):',assembly.read_text(encoding='latin1'),re.M)}
    rom=ROM.from_file(str(a.rom))
    d,_=_disassemble_to_fixpoint(rom,seeds)
    aux=set(_load_aux(str(P/'StreetsOfRageRecompilation/code-analysis/aux_addresses.txt')))
    interiors={i for addr,ins in d.instructions.items() for i in range(addr+1,ins.next_address)}
    bad=(aux-set(d.instructions)) & interiors
    expected={int(x,16) for x in '145EC 145F8 146F0 146FC 1472E 1477A 147A6 148C8 149AA 149DA 14A08 14A0E 14A10 14A56 14A8A 14A96 14AFA 14B1E 14B98 14BEC 14C38 14CB4 14CDC 14D04 14D40'.split()}
    if bad!=expected: raise SystemExit('Unexpected seed conflict set; re-audit pinned inputs')
    # 14BDC is also an invalid instruction seed (decoder raises), never a SoR1 opcode entry.
    bad.add(0x14bdc)
    out=ROOT/'build'; out.mkdir(exist_ok=True)
    repaired=out/'repaired-aux.txt'
    repaired.write_text(''.join(f'{v:06X}\n' for v in sorted((aux-bad)|seeds)))
    (out/'generation-audit.json').write_text(json.dumps(dict(rom=identity,
        removed_upstream_seeds=[f'{x:06X}' for x in sorted(bad)],assembly_seeds=len(seeds)),indent=2)+'\n')
    ca=P/'StreetsOfRageRecompilation/code-analysis'
    return recompile([str(a.rom.resolve()),'--aux',str(repaired),'--labels-csv',str(ca/'labels.csv'),
        '--addresses-csv',str(ca/'addresses.csv'),'--manual-functions',str(ca/'manual_functions.txt'),
        '-o',str(P/'StreetsOfRageRecompilation/generated')])
if __name__=='__main__': sys.exit(main())
