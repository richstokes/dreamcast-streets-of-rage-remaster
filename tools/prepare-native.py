#!/usr/bin/env python3
"""Stage pinned/generated sources locally, excluding SDL interfaces and host orchestration."""
from pathlib import Path
import shutil
import filecmp
import os
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
for f in ['CPU68K.hpp','SoRCheats.hpp','SoRCheats.cpp','SoRControls.cpp','SoRManualFunctions.cpp','SoRInteractions.cpp','SoRMainMenus.cpp','SoRDecompress.cpp','SoRSound.cpp']:
    copy(S/f,D/f)
for f in (S/'generated').glob('SoR*'): copy(f,D/f.name)
copy(M/'include/MegaDriveEnvironment/data_types.hpp',D/'data_types.hpp')
for n in ['VDPState','VDPPort','VDPRenderer','VDPTile']:
    source=M/f'include/MegaDriveEnvironment/system/graphics/{n}.hpp'
    copy(source,D/f'{n}.hpp')
    copy(M/f'src/system/graphics/{n}.cpp',D/f'{n}.cpp')

# Sound-only dependencies retain their upstream license headers.
for f in (M/'include/MegaDriveEnvironment/system/sound/mame_ymfm').glob('*'):
    if f.is_file():copy(f,D/f.name)
for f in (M/'src/system/sound/mame_ymfm').glob('*.cpp'):copy(f,D/f.name)
copy(M/'include/MegaDriveEnvironment/system/z80/suzukiplan/z80.hpp',D/'sor_z80.hpp')

if '--dreamcast' in sys.argv:
    audio=os.environ.get('SOR_AUDIO','0')
    if audio not in ('0','1'):raise SystemExit('SOR_AUDIO must be 0 or 1')
    config=D/'sor_audio_config.hpp'
    text='#pragma once\n#define SOR_ENABLE_EXPERIMENTAL_AUDIO '+audio+'\n'
    if not config.exists() or config.read_text()!=text:config.write_text(text)
