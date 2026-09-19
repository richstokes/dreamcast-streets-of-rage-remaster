#!/usr/bin/env python3
"""List state-table targets that the generated code does not translate as entries.

SoR dispatches object and reaction states through word tables: a `lea table,a1`
handed to a shared dispatcher that indexes it by an object field (relative
tables at $25A4, $12B3C and $12B4C; absolute ones at $B186 and $15848). The
recompiler finds most targets from the disassembly, but a target it misses
fails at run time ("Untranslated dispatch"); two-player friendly fire reached
the player reaction $2502 this way.

Table lengths are not recorded, so each table is read until an entry is odd,
out of range or inside an instruction the disassembler decoded. The list can
therefore include entries past a table's real end; those that are decoded
instruction starts are harmless to seed, the others are data. Seeds are
pinned in tools/generate.py (DISPATCH_TABLE_SEEDS).

  tools/audit-dispatch-tables.py ROM   (after tools/generate.py)
"""
import argparse, glob, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
P = ROOT / 'research/StreetsOfRageProject'
RELATIVE = {0x25A4, 0x12B3C, 0x12B4C}
ABSOLUTE = {0xB186: 0, 0x15848: 0x10000}      # base added to each table word


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path)
    a = ap.parse_args()
    rom = a.rom.read_bytes()
    w = lambda x: int.from_bytes(rom[x:x + 2], 'big')
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    s8 = lambda v: v - 0x100 if v & 0x80 else v

    def lea_a1(x):
        op = w(x)
        if op == 0x43F8: return s16(w(x + 2)) & 0xFFFFFF, x + 4
        if op == 0x43F9: return (w(x + 2) << 16) | w(x + 4), x + 6
        if op == 0x43FA: return x + 2 + s16(w(x + 2)), x + 4
        return None, None

    def transfer(x):
        op = w(x)
        if op in (0x4EF8, 0x4EB8): return s16(w(x + 2)) & 0xFFFFFF
        if op in (0x4EF9, 0x4EB9): return (w(x + 2) << 16) | w(x + 4)
        if op in (0x4EFA, 0x4EBA, 0x6000, 0x6100): return x + 2 + s16(w(x + 2))
        if op >> 8 in (0x60, 0x61) and op & 0xFF: return x + 2 + s8(op & 0xFF)
        return None

    generated = ''.join(Path(f).read_text() for f in glob.glob(str(P / 'StreetsOfRageRecompilation/generated/*.cpp')))
    entries = {int(m, 16) for m in re.findall(r'case 0x0*([0-9A-F]+)u:', generated)}
    entries |= {int(m, 16) for m in re.findall(r'(?:sub|loc)_0*([0-9A-F]+)\b', generated)}
    sys.path.insert(0, str(P / 'RageDecompiler'))
    from tools.disassembler.rom import ROM
    from tools.recompiler.main import _disassemble_to_fixpoint
    asm = (ROOT / 'research/sor-disassemblies/Assemblies/Streets of Rage (JUE) (REV 00) [!].asm').read_text(encoding='latin1')
    seeds = {int(m, 16) for m in re.findall(r'^(?:sub|loc|locret)_([0-9A-F]+):', asm, re.M)}
    d, _ = _disassemble_to_fixpoint(ROM.from_file(str(a.rom)), seeds)
    interior = {i for x, ins in d.instructions.items() for i in range(x + 1, ins.next_address)}

    tables = {}
    for x in range(0, len(rom) - 16, 2):
        table, after = lea_a1(x)
        if table is None: continue
        target = transfer(after)
        if target in RELATIVE or target in ABSOLUTE: tables[(table, target)] = x
    missing = {}
    for (table, dispatcher), site in sorted(tables.items()):
        for i in range(40):
            v = w(table + 2 * i)
            if dispatcher in RELATIVE:
                if not 0 < abs(s16(v)) < 0x1000: break
                target = table + s16(v)
            else: target = ABSOLUTE[dispatcher] + v
            if target % 2 or not 0x200 <= target < len(rom) or target in interior: break
            if target not in entries: missing.setdefault(target, []).append((table, i, dispatcher))
    print('%d tables; %d targets without a translated entry' % (len(tables), len(missing)))
    for target, uses in sorted(missing.items()):
        print('%06X %-9s %s' % (target, 'decoded' if target in d.instructions else 'data?',
                                ' '.join('%X[%d]->%X' % u for u in uses[:3])))


if __name__ == '__main__':
    main()
