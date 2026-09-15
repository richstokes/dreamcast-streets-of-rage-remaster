#!/usr/bin/env python3
"""Stage pinned/generated sources locally, excluding SDL interfaces and host orchestration."""
from pathlib import Path
import shutil
import filecmp
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
    text=source.read_text()
    if n=='VDPRenderer':
        needle='    private:\n'
        assert text.count(needle)==1
        text=text.replace(needle,'    const PixelResult *nativeSpriteLine(int y) { buildSpriteLine(y); return spriteLine_; }\n\n'+needle)
    target=D/f'{n}.hpp'
    if not target.exists() or target.read_text()!=text:target.write_text(text)
    copy(M/f'src/system/graphics/{n}.cpp',D/f'{n}.cpp')
