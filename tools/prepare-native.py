#!/usr/bin/env python3
"""Stage the pinned and generated sources for the native build in build/native/upstream.

The recompiler's output and the parts of MegaDriveEnvironment the port uses are
copied with the checked patches applied (tools/*_patches.py), each translated
instruction is charged its MC68000 time (tools/m68k_cycles.py) so VBlanks follow
emulated CPU time, and with --dreamcast the build-time options taken from the
environment are written to sor_audio_config.hpp.
"""
import argparse
import filecmp
import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

from audio_patches import patch as patch_audio
from cheat_patches import HOOKS as CHEAT_HOOKS, patch_generated as patch_cheats
from game_patches import patch as patch_game
from m68k_cycles import instruction_cycles
from patching import replace_once
from rom import LOCKED_SHA256
from sprite_probe_patches import HOOKS as PROBE_HOOKS, patch_generated as patch_sprite_probe
from vdp_patches import patch as patch_vdp

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'research/StreetsOfRageProject'
RECOMPILATION = PROJECT / 'StreetsOfRageRecompilation'
ENVIRONMENT = PROJECT / 'MegaDriveEnvironment'
OUT = ROOT / 'build/native/upstream'

PATCHED_SOURCES = ['CPU68K.hpp', 'SoRCheats.hpp', 'SoRCheats.cpp', 'SoRControls.cpp', 'SoRManualFunctions.cpp',
                   'SoRInteractions.cpp', 'SoRMainMenus.cpp', 'SoRDecompress.cpp', 'SoRSound.cpp']
VDP_UNITS = ['VDPState', 'VDPPort', 'VDPRenderer', 'VDPTile']

# Build-time options with --dreamcast: environment variable, macro, default.
# Each must be 0 or 1.
OPTIONS = [
    ('SOR_AUDIO', 'SOR_ENABLE_AUDIO', '1'),
    ('SOR_DAC_NATIVE', 'SOR_ENABLE_NATIVE_DAC', '1'),
    ('SOR_DAC_AICA', 'SOR_ENABLE_AICA_DAC', '0'),
    ('SOR_AUDIO_PROFILE', 'SOR_ENABLE_AUDIO_PROFILE', '0'),
    ('SOR_PC_PROFILE', 'SOR_ENABLE_PC_PROFILE', '0'),
    # Start in enhanced graphics, smooth animation, dynamic lighting, weather
    # (the options menu still switches each).
    ('SOR_ENHANCED', 'SOR_DEFAULT_ENHANCED', '0'),
    ('SOR_SMOOTH', 'SOR_DEFAULT_SMOOTH', '0'),
    ('SOR_LIGHTING', 'SOR_DEFAULT_LIGHTING', '1'),
    ('SOR_WEATHER', 'SOR_DEFAULT_WEATHER', '0'),
    # Debug only: Dreamcast B toggles the software comparison renderer (about
    # 85 ms a frame, original graphics). Off by default: B is within reach in play.
    ('SOR_SOFTWARE_TOGGLE', 'SOR_SOFTWARE_TOGGLE', '0'),
]


def write_if_changed(target, text):
    if not target.exists() or target.read_text() != text:
        target.write_text(text)


def copy_if_changed(source, target):
    if not target.exists() or not filecmp.cmp(source, target, shallow=False):
        shutil.copy2(source, target)


def locked_rom():
    candidates = [Path(os.environ['SOR_ROM'])] if os.environ.get('SOR_ROM') else []
    candidates += sorted((ROOT / 'original_rom').glob('*')) + [ROOT / 'build/disc/SOR.BIN']
    for c in candidates:
        if c.is_file() and hashlib.sha256(c.read_bytes()).hexdigest() == LOCKED_SHA256:
            return c.read_bytes()
    raise SystemExit('Locked ROM not found; set SOR_ROM (needed for 68000 instruction timing)')


def cycle_table():
    tool = ROOT / 'build/tests/m68k-cycle-table'
    if not tool.exists():
        tool.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([os.environ.get('CC', 'cc'), '-O1', '-I', str(ROOT / 'research/Genesis-Plus-GX/core/m68k'),
                        str(ROOT / 'tools/m68k-cycle-table.c'), '-o', str(tool)], check=True)
    return [int(v) for v in subprocess.run([str(tool)], capture_output=True, text=True, check=True).stdout.split()]


# The recompiler emits each instruction as `// $PC mnemonic operands` followed
# by a block opening with BEFORE_INSTRUCTION. These forms are rewritten to
# charge the instruction's time.
INSTRUCTION = re.compile(r'(// \$([0-9A-F]{6}) ([^\n]*)\n\s*\{\n\s*)BEFORE_INSTRUCTION\b')
INSTRUCTION_HEAD = (r'(?P<head>// \$(?P<pc>[0-9A-F]{6}) %s [^\n]*\n\s*\{\n\s*)BEFORE_INSTRUCTION_AT\(\d+, 0x[0-9A-F]{6}\)\n'
                    r'(?P<ind>[ ]*)if \((?P<cond>[^\n]*)\) \{\n')
BRANCH = re.compile(INSTRUCTION_HEAD % r'b(?!ra|sr)[a-z]{2}\.(?P<size>[sw])'
                    + r'(?P<ind2>[ ]*)(?P<stmt>[^\n]*;)\n(?P<close>[ ]*\}\n)(?P<end>[ ]*\}\n)')
DBCC = re.compile(INSTRUCTION_HEAD % r'db[a-z]{1,2}\.w'
                  + r'(?P<body>[ ]*m_word \w+ = [^\n]*\n[ ]*cpu\(\)\.setDw[^\n]*\n[ ]*if \([^\n]*\) \{\n[ ]*goto \w+;\n[ ]*\}\n)'
                  r'[ ]*\}\n(?P<end>[ ]*\}\n)')
ANY_BRANCH = re.compile(r'// \$[0-9A-F]{6} b(?!ra|sr)[a-z]{2}\.[sw] ')
ANY_DBCC = re.compile(r'// \$[0-9A-F]{6} db[a-z]{1,2}\.w ')
MULTIPLY = re.compile(r'(?P<head>// \$(?P<pc>[0-9A-F]{6}) (?P<op>mul[su])\.w (?P<src>[^,\n]+), d\d\n[ ]*\{\n'
                      r'[ ]*BEFORE_INSTRUCTION_AT\(\d+, 0x[0-9A-F]{6}\)\n)'
                      r'(?P<body>(?P<ind>[ ]*)[^\n]*\n(?:[ ]+[^}\n][^\n]*\n)*)')


def charge_instructions(text, name, cycles_of):
    """Charge every translated instruction of one generated unit its 68000 time."""
    def charge(m):
        n = cycles_of(int(m.group(2), 16), m.group(3))
        if not 4 <= n <= 200:
            raise SystemExit(f'Implausible 68000 time {n} for {m.group(3)!r} in {name}')
        return m.group(1) + 'BEFORE_INSTRUCTION_AT(%d, 0x%s)' % (n, m.group(2))
    text = INSTRUCTION.sub(charge, text)

    # Conditional branches: Bcc.s costs 8 not taken and 10 taken; Bcc.w 12
    # and 10. DBcc costs 12 when the condition holds, 10 when it branches
    # and 14 when the count expires. Charge the cheaper path up front and
    # the difference on the other.
    def bcc(m):
        short = m['size'] == 's'
        extra = 'SOR_EXTRA_CYCLES(2, 0x%s)' % m['pc']
        return (m['head'] + 'BEFORE_INSTRUCTION_AT(%d, 0x%s)\n' % (8 if short else 10, m['pc'])
                + m['ind'] + 'if (' + m['cond'] + ') {\n' + m['ind2'] + (extra + ' ' if short else '') + m['stmt'] + '\n'
                + m['close'] + ('' if short else m['ind'] + extra + '\n') + m['end'])

    def dbcc(m):
        inner = m['ind'] + '    '
        return (m['head'] + 'BEFORE_INSTRUCTION_AT(10, 0x%s)\n' % m['pc'] + m['ind'] + 'if (' + m['cond'] + ') {\n' + m['body']
                + inner + 'SOR_EXTRA_CYCLES(4, 0x%s)\n' % m['pc'] + m['ind'] + '} else {\n'
                + inner + 'SOR_EXTRA_CYCLES(2, 0x%s)\n' % m['pc'] + m['ind'] + '}\n' + m['end'])

    # MULU/MULS with a register or memory multiplier: 2 cycles per set bit
    # (MULU) or per 01/10 pair (MULS), from the multiplier as executed.
    def mul(m):
        source = m['src']
        kind = 'Mulu' if m['op'] == 'mulu' else 'Muls'
        if source.startswith('#'):
            return m[0]
        if re.fullmatch(r'd\d', source):
            value = 'cpu().dw(%s)' % source[1]
        elif 'm_word t0 = memory().read' in m['body'].split('\n')[0]:
            value = 't0'
        else:
            raise SystemExit(f'Unrecognized multiply form in {name}: {m[0]!r}')
        extra = m['ind'] + 'SOR_EXTRA_CYCLES(sor%sBits(%s), 0x%s)\n' % (kind, value, m['pc'])
        if value == 't0':
            first, rest = m['body'].split('\n', 1)
            return m['head'] + first + '\n' + extra + rest
        return m['head'] + extra + m['body']

    text = MULTIPLY.sub(mul, text)
    for form, rewrite, expected in ((BRANCH, bcc, ANY_BRANCH), (DBCC, dbcc, ANY_DBCC)):
        text, count = form.subn(rewrite, text)
        if count != len(expected.findall(text)):
            raise SystemExit(f'Unrecognized branch form in {name}')
    return text


# Host analysis (state-synchronised comparisons): access to the register file.
EXCHANGE_CPU_STATE = '''    void exchangeCpuState(uint32_t *regs, bool load) override {
        for (int i = 0; i < 8; i++) { if (load) cpu_.d[i] = regs[i]; else regs[i] = cpu_.d[i]; }
        for (int i = 0; i < 7; i++) { if (load) cpu_.a[i] = regs[8 + i]; else regs[8 + i] = cpu_.a[i]; }
        if (load) { cpu_.ssp = regs[15]; cpu_.setStatus(m_word(regs[16])); }
        else { regs[15] = cpu_.ssp; regs[16] = cpu_.status(); }
    }

'''
BEFORE_INSTRUCTION = '#define BEFORE_INSTRUCTION if (irqLevel() > cpu().interruptMask()) serviceIRQ(); pace();'
TIMED_INSTRUCTIONS = BEFORE_INSTRUCTION + '''
#define BEFORE_INSTRUCTION_CYCLES(n) settleInstruction(); if (irqLevel() > cpu().interruptMask()) serviceIRQ(); startInstruction(n);
#ifdef SOR_PC_HISTOGRAM
#define BEFORE_INSTRUCTION_AT(n, pc) pcHistogram(pc, n); BEFORE_INSTRUCTION_CYCLES(n)
#define SOR_EXTRA_CYCLES(n, pc) pcHistogram(pc, n); extendInstruction(n);
#else
#define BEFORE_INSTRUCTION_AT(n, pc) BEFORE_INSTRUCTION_CYCLES(n)
#define SOR_EXTRA_CYCLES(n, pc) extendInstruction(n);
#endif
// MULU: 2 cycles per set multiplier bit; MULS: 2 per 01/10 pair.
inline unsigned sorMuluBits(unsigned v){return 2u*unsigned(__builtin_popcount(v&0xFFFFu));}
inline unsigned sorMulsBits(unsigned v){return 2u*unsigned(__builtin_popcount(((v<<1)^v)&0xFFFFu));}'''


def stage_generated():
    generated = RECOMPILATION / 'generated'
    # Units of an earlier partitioning that this generation did not produce.
    for f in OUT.glob('SoR-*'):
        if f.suffix in ('.cpp', '.hpp') and not (generated / f.name).exists():
            f.unlink()
    rom, table = locked_rom(), cycle_table()
    cycles_of = lambda address, text: instruction_cycles(rom, table, address, text)
    probe_hooks = cheat_hooks = 0
    for f in generated.glob('SoR*'):
        text = f.read_text()
        if f.suffix == '.cpp':
            text, n = patch_cheats(text)
            cheat_hooks += n
            text, n = patch_sprite_probe(text)
            probe_hooks += n
            text = charge_instructions(text, f.name, cycles_of)
        elif f.name == 'SoR.hpp':
            anchor = '    int cpuInterruptMask() const override {'
            text = replace_once(text, anchor, EXCHANGE_CPU_STATE + anchor)
        elif f.name == 'SoR-common.hpp':
            text = replace_once(text, BEFORE_INSTRUCTION, TIMED_INSTRUCTIONS)
        write_if_changed(OUT / f.name, text)
    if (probe_hooks, cheat_hooks) != (PROBE_HOOKS, CHEAT_HOOKS):
        raise SystemExit(f'Hooks placed in the generated code: {probe_hooks} sprite probe (expected {PROBE_HOOKS}), '
                         f'{cheat_hooks} cheat (expected {CHEAT_HOOKS}); the recompiler output has changed')


def stage_pinned():
    for name in PATCHED_SOURCES:
        write_if_changed(OUT / name, patch_game(name, (RECOMPILATION / name).read_text()))
    copy_if_changed(ENVIRONMENT / 'include/MegaDriveEnvironment/data_types.hpp', OUT / 'data_types.hpp')
    for unit in VDP_UNITS:
        for source in (ENVIRONMENT / f'include/MegaDriveEnvironment/system/graphics/{unit}.hpp',
                       ENVIRONMENT / f'src/system/graphics/{unit}.cpp'):
            write_if_changed(OUT / source.name, patch_vdp(source.name, source.read_text()))
    # Sound-only dependencies retain their upstream license headers.
    for directory, pattern in [('include/MegaDriveEnvironment/system/sound/mame_ymfm', '*'),
                               ('src/system/sound/mame_ymfm', '*.cpp')]:
        for f in (ENVIRONMENT / directory).glob(pattern):
            if f.is_file():
                write_if_changed(OUT / f.name, patch_audio(f.name, f.read_text()))
    z80 = ENVIRONMENT / 'include/MegaDriveEnvironment/system/z80/suzukiplan/z80.hpp'
    write_if_changed(OUT / 'sor_z80.hpp', patch_audio(z80.name, z80.read_text()))


def write_config():
    lines = ['#pragma once']
    for variable, macro, default in OPTIONS:
        value = os.environ.get(variable, default)
        if value not in ('0', '1'):
            raise SystemExit(f'{variable} must be 0 or 1')
        lines.append(f'#define {macro} {value}')
    # Limit the profiler's gameplay bucket to gameplay frames FIRST:LAST (0:0 is all).
    frames = os.environ.get('SOR_PC_PROFILE_FRAMES', '0:0')
    if not re.fullmatch(r'\d+:\d+', frames):
        raise SystemExit('SOR_PC_PROFILE_FRAMES must be FIRST:LAST')
    first, last = frames.split(':')
    lines += [f'#define SOR_PC_PROFILE_FIRST {first}', f'#define SOR_PC_PROFILE_LAST {last}']
    write_if_changed(OUT / 'sor_audio_config.hpp', '\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dreamcast', action='store_true', help='also write sor_audio_config.hpp from the SOR_* environment')
    a = ap.parse_args()
    if not (RECOMPILATION / 'generated/SoR.hpp').exists():
        raise SystemExit('Run tools/generate.py with the locked ROM first')
    OUT.mkdir(parents=True, exist_ok=True)
    stage_pinned()
    stage_generated()
    if a.dreamcast:
        write_config()


if __name__ == '__main__':
    main()
