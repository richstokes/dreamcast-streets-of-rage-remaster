#!/usr/bin/env python3
"""Stage pinned/generated sources locally, excluding SDL interfaces and host orchestration."""
from pathlib import Path
import shutil
import filecmp
import os
import json
import re
import sys
def copy(source,target):
    if not target.exists() or not filecmp.cmp(source,target,shallow=False): shutil.copy2(source,target)
R=Path(__file__).resolve().parents[1]; P=R/'research/StreetsOfRageProject'
D=R/'build/native/upstream'; D.mkdir(parents=True,exist_ok=True)
S=P/'StreetsOfRageRecompilation'; M=P/'MegaDriveEnvironment'
if not (S/'generated/SoR.hpp').exists(): raise SystemExit('Run tools/generate.py with the locked ROM first')
# Remove obsolete generated units when partitioning changes between revisions.
for f in D.glob('SoR-*'):
    if f.suffix in ('.cpp','.hpp') and not (S/'generated'/f.name).exists(): f.unlink()
sys.path.insert(0,str(R/'tools'))
from game_patches import patch as patch_game
for f in ['CPU68K.hpp','SoRCheats.hpp','SoRCheats.cpp','SoRControls.cpp','SoRManualFunctions.cpp','SoRInteractions.cpp','SoRMainMenus.cpp','SoRDecompress.cpp','SoRSound.cpp']:
    text=patch_game(f,(S/f).read_text());target=D/f
    if not target.exists() or target.read_text()!=text:target.write_text(text)
# Charge each translated instruction its MC68000 time (tools/m68k_cycles.py),
# from the recompiler's source comment, so VBlanks follow emulated CPU time.
sys.path.insert(0,str(R/'tools'))
from m68k_cycles import instruction_cycles
import hashlib,subprocess
def locked_rom():
    wanted=json.loads((R/'tools/upstream-lock.json').read_text()).get('rom_sha256','dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0')
    candidates=[Path(os.environ['SOR_ROM'])] if os.environ.get('SOR_ROM') else []
    candidates+=sorted((R/'original_rom').glob('*'))+[R/'build/disc/SOR.BIN',R/'local/SOR.bin']
    for c in candidates:
        if c.is_file() and hashlib.sha256(c.read_bytes()).hexdigest()==wanted:return c.read_bytes()
    raise SystemExit('Locked ROM not found; set SOR_ROM (needed for 68000 instruction timing)')
def cycle_table():
    tool=R/'build/tests/m68k-cycle-table'
    if not tool.exists():
        tool.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['cc','-O1','-I',str(R/'research/Genesis-Plus-GX/core/m68k'),str(R/'tools/m68k-cycle-table.c'),'-o',str(tool)],check=True)
    return [int(v) for v in subprocess.run([str(tool)],capture_output=True,text=True,check=True).stdout.split()]
ROM_BYTES=locked_rom();CYCLE_TABLE=cycle_table()
m68k_cycles=lambda address,text:instruction_cycles(ROM_BYTES,CYCLE_TABLE,address,text)
instruction=re.compile(r'(// \$([0-9A-F]{6}) ([^\n]*)\n\s*\{\n\s*)BEFORE_INSTRUCTION\b')
for f in (S/'generated').glob('SoR*'):
    text=f.read_text()
    if f.suffix=='.cpp':
        def charge(m):
            n=m68k_cycles(int(m.group(2),16),m.group(3))
            if not 4<=n<=200:raise SystemExit(f'Implausible 68000 time {n} for {m.group(3)!r} in {f.name}')
            return m.group(1)+'BEFORE_INSTRUCTION_CYCLES(%d)'%n
        text=instruction.sub(charge,text)
    elif f.name=='SoR-common.hpp':
        text=text.replace('#define BEFORE_INSTRUCTION if (irqLevel() > cpu().interruptMask()) serviceIRQ(); pace();',
            '#define BEFORE_INSTRUCTION if (irqLevel() > cpu().interruptMask()) serviceIRQ(); pace();\n'
            '#define BEFORE_INSTRUCTION_CYCLES(n) if (irqLevel() > cpu().interruptMask()) serviceIRQ(); pace(n);')
        assert 'BEFORE_INSTRUCTION_CYCLES' in text
    target=D/f.name
    if not target.exists() or target.read_text()!=text:target.write_text(text)
copy(M/'include/MegaDriveEnvironment/data_types.hpp',D/'data_types.hpp')
sys.path.insert(0,str(R/'tools'))
from vdp_patches import patch as patch_vdp
for n in ['VDPState','VDPPort','VDPRenderer','VDPTile']:
    source=M/f'include/MegaDriveEnvironment/system/graphics/{n}.hpp';target=D/f'{n}.hpp'
    text=patch_vdp(source.name,source.read_text())
    if not target.exists() or target.read_text()!=text:target.write_text(text)
    source=M/f'src/system/graphics/{n}.cpp';target=D/f'{n}.cpp'
    text=patch_vdp(source.name,source.read_text())
    if not target.exists() or target.read_text()!=text:target.write_text(text)

# Sound-only dependencies retain their upstream license headers.
from audio_patches import patch as patch_audio
for directory,pattern in [('include/MegaDriveEnvironment/system/sound/mame_ymfm','*'),
                          ('src/system/sound/mame_ymfm','*.cpp')]:
    for f in (M/directory).glob(pattern):
        if not f.is_file():continue
        text=patch_audio(f.name,f.read_text())
        target=D/f.name
        if not target.exists() or target.read_text()!=text:target.write_text(text)
copy(M/'include/MegaDriveEnvironment/system/z80/suzukiplan/z80.hpp',D/'sor_z80.hpp')

if '--dreamcast' in sys.argv:
    audio=os.environ.get('SOR_AUDIO','1')
    if audio not in ('0','1'):raise SystemExit('SOR_AUDIO must be 0 or 1')
    native=os.environ.get('SOR_DAC_NATIVE','1')
    if native not in ('0','1'):raise SystemExit('SOR_DAC_NATIVE must be 0 or 1')
    split=os.environ.get('SOR_DAC_AICA','0')
    if split not in ('0','1'):raise SystemExit('SOR_DAC_AICA must be 0 or 1')
    profile=os.environ.get('SOR_AUDIO_PROFILE','0')
    if profile not in ('0','1'):raise SystemExit('SOR_AUDIO_PROFILE must be 0 or 1')
    pcprofile=os.environ.get('SOR_PC_PROFILE','0')
    if pcprofile not in ('0','1'):raise SystemExit('SOR_PC_PROFILE must be 0 or 1')
    config=D/'sor_audio_config.hpp'
    text='#pragma once\n#define SOR_ENABLE_EXPERIMENTAL_AUDIO '+audio+'\n#define SOR_ENABLE_NATIVE_DAC '+native+'\n#define SOR_ENABLE_AICA_DAC '+split+'\n#define SOR_ENABLE_AUDIO_PROFILE '+profile+'\n#define SOR_ENABLE_PC_PROFILE '+pcprofile+'\n'
    if not config.exists() or config.read_text()!=text:config.write_text(text)
