#!/usr/bin/env python3
"""Generate SoR with disassembly-checked entry points, retaining strict opcode errors."""
import argparse
import json
from pathlib import Path
import re
import sys
from rom import inspect, LOCKED_SHA256
ROOT=Path(__file__).resolve().parents[1]
P=ROOT/'research/StreetsOfRageProject'

# State-table targets the disassembly-seeded pass does not emit as entries
# (tools/audit-dispatch-tables.py). Two-player friendly fire reached reaction
# $2502 and stopped with "Untranslated dispatch". All are decoded instruction
# starts; any past a table's real end is an unused but harmless entry. The
# second group are inline `jmp table(pc,dn)` targets; $109DC/$109EA (the
# player-mask table at $109A8, reached by a two-player continue) are code the
# disassembly never reached.
DISPATCH_TABLE_SEEDS={int(x,16) for x in '''
002D54 002D5E 002FE2 003D04 005526 00AA20 00BAEC 00BAF8 00BB06 01206C 013658
0109DC 0109EA
000536 000828 0008E8 00227C 0024E6 002502 0025FE 004EBA 007204 00AA58
00BBB0 00BBCE 00BC0A 00BC28 00BC78 00BC96 00C1AC 00C3C4 00C648 00C710
00C786 00C800 00C876 00C8CC 00C904 00D14C 00D18A 00D24C 00D5F8 00F8C4
00F914 01317C 0132B2 0132C4 013356 0133AA 0133EC 01342A 013448 014296
0142B2 014348 01460E 014712 014BFE 015176 015A96
'''.split()}

def verify_sprite_entry(directory):
    """Reject partitions that silently ignore the manual SAT-building entry."""
    functions={}
    for path in directory.glob('SoR-*.cpp'):
        for match in re.finditer(r'^void StreetsOfRage::(\w+)\(m_long entry_\) \{\n(.*?)^\}',path.read_text(),re.M|re.S):
            functions[match[1]]=match[2]
    name='enqueue_object_render_bucket'
    visited=set()
    while name not in visited:
        visited.add(name)
        body=functions.get(name,'')
        if re.search(r'case 0x0*AE96u:',body):
            return
        alias=re.fullmatch(r'\s*(\w+)\(entry_\);\s*',body)
        if not alias: break
        name=alias[1]
    raise RuntimeError('Manual sprite entry AE96 is not routed by generated function partitions')

def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('rom',type=Path)
    a=ap.parse_args(); identity=inspect(a.rom.read_bytes())
    if not identity['known']:
        ap.error(f'The seed repairs are validated only against the locked ROM {LOCKED_SHA256}')
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
    repaired.write_text(''.join(f'{v:06X}\n' for v in sorted((aux-bad)|{v for v in seeds if 0x143D0<=v<0x158C4}|DISPATCH_TABLE_SEEDS)))
    (out/'generation-audit.json').write_text(json.dumps(dict(rom=identity,
        removed_upstream_seeds=[f'{x:06X}' for x in sorted(bad)],assembly_seeds=len(seeds)),indent=2)+'\n')
    ca=P/'StreetsOfRageRecompilation/code-analysis'
    result=recompile([str(a.rom.resolve()),'--aux',str(repaired),'--labels-csv',str(ca/'labels.csv'),
        '--addresses-csv',str(ca/'addresses.csv'),'--manual-functions',str(ca/'manual_functions.txt'),
        '-o',str(P/'StreetsOfRageRecompilation/generated')])
    if result in (None,0): verify_sprite_entry(P/'StreetsOfRageRecompilation/generated')
    return result
if __name__=='__main__': sys.exit(main())
