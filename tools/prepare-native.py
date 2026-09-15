#!/usr/bin/env python3
"""Stage pinned/generated sources locally, excluding SDL interfaces and host orchestration."""
from pathlib import Path
import shutil
R=Path(__file__).resolve().parents[1]; P=R/'research/StreetsOfRageProject'
D=R/'build/native/upstream'; D.mkdir(parents=True,exist_ok=True)
S=P/'StreetsOfRageRecompilation'; M=P/'MegaDriveEnvironment'
for f in ['CPU68K.hpp','SoRCheats.hpp','SoRCheats.cpp','SoRControls.cpp','SoRManualFunctions.cpp','SoRInteractions.cpp','SoRMainMenus.cpp','SoRDecompress.cpp','SoRSound.cpp']:
    shutil.copy2(S/f,D/f)
for f in (S/'generated').glob('SoR*'): shutil.copy2(f,D/f.name)
shutil.copy2(M/'include/MegaDriveEnvironment/data_types.hpp',D/'data_types.hpp')
for n in ['VDPState','VDPPort','VDPRenderer','VDPTile']:
    shutil.copy2(M/f'include/MegaDriveEnvironment/system/graphics/{n}.hpp',D/f'{n}.hpp')
    shutil.copy2(M/f'src/system/graphics/{n}.cpp',D/f'{n}.cpp')
# Generated code and VDP components are unchanged. Replacement headers resolve via -I.
if not (D/'SoR.hpp').exists(): raise SystemExit('Run tools/generate.py with the locked ROM first')

# Measured SH-4 hotspot; retain the upstream reference renderer for differential tests.
renderer=D/'VDPRenderer.cpp'
text=renderer.read_text();start=text.index('void VDPRenderer::buildPlaneLine(');end=text.index('void VDPRenderer::overlayWindowLine(',start)
renderer.write_text(text[:start]+(R/'src/dreamcast/plane_line.inc').read_text()+'\n'+text[end:])
