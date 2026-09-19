"""MC68000 execution times for translated instructions.

`instruction_cycles` reads each instruction's opcode from the ROM and takes its
base time (including effective-address calculation) from the Musashi cycle table
in the Genesis Plus GX research checkout (tools/m68k-cycle-table.c), adding the
statically known extras: MOVEM register counts and immediate shift counts.
Conditional branches are charged as taken and DBcc as looping; register shift
counts, MULU/MULS and DIVU/DIVS use typical values. `cycles` estimates the same
from the recompiler's comment text alone and serves as the fallback. The result
drives emulated 68000 time (VBlank cadence), not cycle-exact execution.
"""
import re

# Effective-address calculation time: (byte/word, long).
EA = {'dn': (0, 0), 'an': (0, 0), 'ind': (4, 8), 'post': (4, 8), 'pre': (6, 10), 'd16': (8, 12),
      'idx': (10, 14), 'absw': (8, 12), 'absl': (12, 16), 'imm': (4, 8)}
# Destination-side cost for MOVE (table 8-2/8-3 minus the source column).
MOVE_DST = {'dn': (0, 0), 'an': (0, 0), 'ind': (4, 8), 'post': (4, 8), 'pre': (4, 8), 'd16': (8, 12),
            'idx': (10, 14), 'absw': (8, 12), 'absl': (12, 16)}


def ea_kind(op):
    op = op.strip()
    if re.fullmatch(r'd[0-7]', op): return 'dn'
    if re.fullmatch(r'(a[0-7]|sp)', op): return 'an'
    if op.startswith('#'): return 'imm'
    if re.fullmatch(r'\((a[0-7]|sp)\)', op): return 'ind'
    if re.fullmatch(r'\((a[0-7]|sp)\)\+', op): return 'post'
    if re.fullmatch(r'-\((a[0-7]|sp)\)', op): return 'pre'
    if re.fullmatch(r'-?\$?[0-9a-fA-F]*\((a[0-7]|sp|pc)\)', op): return 'd16'
    if '(' in op: return 'idx'
    m = re.fullmatch(r'-?\$([0-9a-fA-F]+)', op)
    if m:
        value = int(m.group(1), 16)
        return 'absw' if value < 0x8000 or value >= 0xFF8000 else 'absl'
    return 'absl'


def split_operands(text):
    parts, depth, cur = [], 0, ''
    for ch in text:
        if ch == '(': depth += 1
        if ch == ')': depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): parts.append(cur.strip())
    return parts


def register_count(text):
    count = 0
    for part in text.split('/'):
        m = re.fullmatch(r'([da])([0-7])-([da])([0-7])', part.strip())
        index = lambda kind, n: int(n) + (8 if kind == 'a' else 0)   # d0-d7, then a0-a7
        count += (index(m.group(3), m.group(4)) - index(m.group(1), m.group(2)) + 1) if m else 1
    return count


def cycles(instruction):
    """Cycles for one instruction comment such as 'move.w d6, $18(a1)'."""
    parts = instruction.strip().split(None, 1)
    mnemonic = parts[0].lower()
    ops = split_operands(parts[1]) if len(parts) > 1 else []
    name, _, size = mnemonic.partition('.')
    long = size == 'l'
    kinds = [ea_kind(o) for o in ops]
    ea = lambda k: EA.get(k, (8, 12))[1 if long else 0]
    src = kinds[0] if kinds else 'dn'
    dst = kinds[-1] if kinds else 'dn'

    if name in ('move', 'movea'):
        if name == 'move' and not size:
            return 12   # move to/from SR/CCR/USP
        return 4 + ea(src) + MOVE_DST.get(dst, (8, 12))[1 if long else 0]
    if name == 'moveq': return 4
    if name == 'lea': return {'ind': 4, 'd16': 8, 'idx': 12, 'absw': 8, 'absl': 12}.get(src, 8)
    if name == 'pea': return {'ind': 12, 'd16': 16, 'idx': 20, 'absw': 16, 'absl': 20}.get(src, 16)
    if name == 'jsr': return {'ind': 16, 'd16': 18, 'idx': 22, 'absw': 18, 'absl': 20}.get(src, 20)
    if name == 'jmp': return {'ind': 8, 'd16': 10, 'idx': 14, 'absw': 10, 'absl': 12}.get(src, 12)
    if name == 'bsr': return 18
    if name == 'bra': return 10
    if name.startswith('db'): return 10
    if name.startswith('b') and name not in ('btst', 'bset', 'bclr', 'bchg'): return 10
    if name == 'rts': return 16
    if name == 'rte': return 20
    if name == 'nop': return 4
    if name in ('ext', 'swap'): return 4
    if name == 'exg': return 6
    if name in ('link',): return 16
    if name in ('unlk',): return 12
    if name == 'movem':
        reglist = lambda o: re.fullmatch(r'[da][0-7]([-/][da][0-7])*', o.strip()) is not None
        to_memory = reglist(ops[0])
        mem = dst if to_memory else src
        n = register_count(ops[0] if to_memory else ops[-1])
        per = 8 if long else 4
        if to_memory:
            return {'ind': 8, 'pre': 8, 'd16': 12, 'idx': 14, 'absw': 12, 'absl': 16}.get(mem, 12) + per * n
        return {'ind': 12, 'post': 12, 'd16': 16, 'idx': 18, 'absw': 16, 'absl': 20}.get(mem, 16) + per * n
    if name in ('clr', 'neg', 'negx', 'not'):
        return (6 if long else 4) if dst == 'dn' else (12 if long else 8) + ea(dst)
    if name == 'tst': return 4 + ea(dst)
    if name in ('scc', 'st', 'sf') or re.fullmatch(r's(hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)', name):
        return 5 if dst == 'dn' else 8 + ea(dst)
    if name in ('btst', 'bset', 'bclr', 'bchg'):
        immediate = src == 'imm'
        if dst == 'dn':
            return {'btst': 10, 'bset': 12, 'bchg': 12, 'bclr': 14}[name] if immediate else \
                   {'btst': 6, 'bset': 8, 'bchg': 8, 'bclr': 10}[name]
        return (8 if name == 'btst' else 12) + ea(dst) if immediate else (4 if name == 'btst' else 8) + ea(dst)
    if name in ('lsl', 'lsr', 'asl', 'asr', 'rol', 'ror', 'roxl', 'roxr'):
        if len(ops) == 1:
            return 8 + ea(dst)
        n = int(ops[0].lstrip('#$'), 16) if src == 'imm' else 4
        return (8 if long else 6) + 2 * n
    if name in ('mulu', 'muls'): return 54 + ea(src)
    if name == 'divu': return 110 + ea(src)
    if name == 'divs': return 130 + ea(src)
    if name in ('abcd', 'sbcd', 'addx', 'subx'): return 18 if src == 'pre' else (8 if long else 4) + 2
    if name in ('addq', 'subq'):
        if dst == 'dn': return 8 if long else 4
        if dst == 'an': return 8
        return (12 if long else 8) + ea(dst)
    if name in ('addi', 'subi', 'andi', 'ori', 'eori', 'cmpi'):
        if dst == 'dn':
            return (14 if name == 'cmpi' else 16) if long else 8
        if name == 'cmpi':
            return (12 if long else 8) + ea(dst)
        return (20 if long else 12) + ea(dst)
    if name in ('adda', 'suba', 'cmpa'):
        base = 6 if name == 'cmpa' else (8 if size == 'w' else 6)
        return base + ea(src) + (2 if long and src in ('dn', 'an', 'imm') and name != 'cmpa' else 0)
    if name in ('add', 'sub', 'and', 'or', 'eor', 'cmp'):
        if dst == 'dn':
            return (6 + ea(src) + (2 if src in ('dn', 'an', 'imm') else 0)) if long else 4 + ea(src)
        return (12 if long else 8) + ea(dst)
    return 8


def instruction_cycles(rom, table, address, text):
    """Musashi base time for the opcode at `address`, plus static extras."""
    op = (rom[address] << 8) | rom[address + 1]
    base = table[op]
    if base == 0:
        return cycles(text)
    if op & 0xFB80 == 0x4880:                       # MOVEM: per register
        mask = (rom[address + 2] << 8) | rom[address + 3]
        return base + bin(mask).count('1') * (8 if op & 0x40 else 4)
    if op & 0xF000 == 0xE000 and op & 0xC0 != 0xC0:  # register shifts/rotates
        count = ((op >> 9) & 7 or 8) if not op & 0x20 else 4
        return base + 2 * count
    if op & 0xF1C0 in (0xC0C0, 0xC1C0):              # MULU/MULS: 2 per set bit, typical 8
        return base + 16
    if op & 0xF1C0 in (0x80C0, 0x81C0):              # DIVU/DIVS: data dependent
        return max(base, cycles(text))
    return base
