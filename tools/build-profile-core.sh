#!/bin/sh
# Build a Genesis Plus GX core with a per-PC 68000 cycle histogram (analysis only;
# the reference core in research/ is left untouched). Output:
# build/gpgx-profile/genesis_plus_gx_libretro.dylib, used with genesis_reference.py --profile.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$root/tools/check-requirements.sh" host research
rm -rf "$root/build/gpgx-profile"
cp -R "$root/research/Genesis-Plus-GX" "$root/build/gpgx-profile"
cat "$root/tools/gpgx-profile-hook.c" >> "$root/build/gpgx-profile/core/debug/cpuhook.c"
# YM2612 write log (68000 and Z80 writes, with their master-clock time).
python3 - "$root/build/gpgx-profile/core/sound/sound.c" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1]); t = p.read_text()
old = 'static void YM2612_Write(unsigned int cycles, unsigned int a, unsigned int v)\n{\n'
assert old in t
t = t.replace(old, 'extern void sor_ym_log(unsigned int, unsigned int, unsigned int);\n' + old + '  sor_ym_log(cycles, a, v);\n', 1)
p.write_text(t)
m = Path(sys.argv[1]).parent.parent / 'mem68k.c'; t = m.read_text()
old = '      zram[address & 0x1FFF] = data;\n      return;'
assert t.count(old) == 1
t = t.replace(old, '      zram[address & 0x1FFF] = data;\n      sor_ym_log(m68k.cycles, 0x100 | (address & 0x1FFF), data);\n      return;')
t = 'extern void sor_ym_log(unsigned int, unsigned int, unsigned int);\n' + t
m.write_text(t)
gen = Path(sys.argv[1]).parent.parent / 'genesis.c'; t = gen.read_text()
old = 'void gen_zbusreq_w(unsigned int data, unsigned int cycles)\n{\n'
assert t.count(old) == 1
t = 'extern void sor_ym_log(unsigned int, unsigned int, unsigned int);\n' + t.replace(old, old + '  sor_ym_log(cycles, 0x4000, data);\n')
gen.write_text(t)
vdp = Path(sys.argv[1]).parent.parent / 'vdp_ctrl.c'; t = vdp.read_text()
# 68000-bus DMA timeframes (port $8000: 1 at the start, 0 at the end).
old = '  /* Check if 68k bus is accessed by DMA */\n  if (dma_type < 2)\n  {\n'
assert t.count(old) == 1
t = 'extern void sor_ym_log(unsigned int, unsigned int, unsigned int);\n' + t.replace(
    old, old + '    sor_ym_log(cycles, 0x8000, 1); sor_ym_log(dma_endCycles, 0x8000, 0);\n')
vdp.write_text(t + '\n/* SoR analysis: control-port latches for state export. */\n'
               'void sor_vdp_latches(unsigned int *out) { out[0] = addr; out[1] = code; out[2] = pending; }\n')
# Z80 reads of the 68000 bus held by DMA (port $8001: the Z80 time it waited from).
z = Path(sys.argv[1]).parent.parent / 'memz80.c'; t = z.read_text()
old = '    /* force Z80 to wait until end of DMA */\n    Z80.cycles = dma_endCycles;\n'
assert t.count(old) == 1
z.write_text('extern void sor_ym_log(unsigned int, unsigned int, unsigned int);\n' +
             t.replace(old, '    sor_ym_log(Z80.cycles, 0x8001, 1);\n' + old))
PY
cd "$root/build/gpgx-profile"
make -f Makefile.libretro clean > /dev/null
make -f Makefile.libretro HOOK_CPU=1 -j"${JOBS:-8}"
