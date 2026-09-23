/* Appended to Genesis Plus GX's core/debug/cpuhook.c by tools/build-profile-core.sh
   for the profiling core (HOOK_CPU builds only; not part of the game):
   per-PC 68000 cycle histogram, call timeline, YM2612 write log and machine-state
   export. Driven from tools/genesis_reference.py. */
#ifdef HOOK_CPU
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../m68k/m68k.h"
#include "../z80/z80.h"

#define SOR_CLOCKS_PER_FRAME 896040   /* master clocks per NTSC frame */
#define SOR_FIRST_FRAME_START 784184  /* master clocks into the first emulated frame at which the 68000 starts (genesis.c) */

/* Per-PC histogram of 68000 cycles. */
static unsigned long long sor_pc_cycles[0x40000];
static unsigned int sor_last_pc;
static int sor_last_cycles, sor_active;

/* Call timeline: master-clock time of each entry to a watched address. */
typedef struct { unsigned int pc, frame; int cycles; } sor_watch_entry;
static unsigned char sor_watched[0x40000];
static sor_watch_entry *sor_watch_log;
static unsigned int sor_watch_count, sor_watch_cap, sor_watch_frame_no;
static int sor_watching;
static unsigned int sor_prev_pc = 0xffffffffu;

/* YM2612 writes: frame, master clock within the frame, port, value. */
static unsigned int *sor_ym_entries, sor_ym_count, sor_ym_cap, sor_ym_frame_no;
static int sor_ym_active;

/* Entered by JSR, JMP or BSR, or forward (fall-through or a forward branch), not
   by a loop's backward branch; the VBlank handler is recorded at its vector target. */
static int sor_called(unsigned int pc)
{
  unsigned int op;
  if (pc >= 0x80000) return 0;
  op = *(unsigned short *)(m68k.memory_map[(pc >> 16) & 0xff].base + (pc & 0xffff));
  return (op & 0xFFC0) == 0x4E80 || (op & 0xFFC0) == 0x4EC0 || (op & 0xFF00) == 0x6100;
}

static void sor_hook(hook_type_t type, int width, unsigned int address, unsigned int value)
{
  (void)width; (void)value;
  if (type != HOOK_M68K_E) return;
  if (sor_watching && address < 0x80000 && sor_watched[address >> 1] && sor_watch_count < sor_watch_cap &&
      (address == 0x19d16 || sor_prev_pc < address || sor_called(sor_prev_pc))) {
    sor_watch_log[sor_watch_count].pc = address; sor_watch_log[sor_watch_count].frame = sor_watch_frame_no;
    sor_watch_log[sor_watch_count++].cycles = m68k.cycles;
  }
  sor_prev_pc = address;
  if (!sor_active) return;
  if (sor_last_pc < 0x80000) {
    int delta = m68k.cycles - sor_last_cycles;
    if (delta > 0 && delta < 2000000) sor_pc_cycles[sor_last_pc >> 1] += (unsigned long long)delta;
  }
  sor_last_pc = address; sor_last_cycles = m68k.cycles;
}

__attribute__((visibility("default"))) void sor_profile_start(void)
{
  memset(sor_pc_cycles, 0, sizeof(sor_pc_cycles)); sor_active = 1; sor_last_pc = 0xffffffffu; set_cpu_hook(sor_hook);
}

__attribute__((visibility("default"))) int sor_profile_stop(const char *path)
{
  FILE *f; sor_active = 0; set_cpu_hook(NULL);
  f = fopen(path, "wb"); if (!f) return -1;
  fwrite(sor_pc_cycles, sizeof(sor_pc_cycles[0]), 0x40000, f); fclose(f); return 0;
}

__attribute__((visibility("default"))) void sor_watch_start(const unsigned int *pcs, int n)
{
  int i; memset(sor_watched, 0, sizeof(sor_watched));
  for (i = 0; i < n; i++) if (pcs[i] < 0x80000) sor_watched[pcs[i] >> 1] = 1;
  sor_watch_cap = 1u << 24; sor_watch_count = 0;
  sor_watch_log = (sor_watch_entry *)realloc(sor_watch_log, sizeof(sor_watch_entry) * sor_watch_cap);
  sor_watching = 1; set_cpu_hook(sor_hook);
}

__attribute__((visibility("default"))) void sor_watch_frame(unsigned int frame) { sor_watch_frame_no = frame; }

__attribute__((visibility("default"))) int sor_watch_stop(const char *path)
{
  unsigned int i; FILE *f; sor_watching = 0; if (!sor_active) set_cpu_hook(NULL);
  f = fopen(path, "w"); if (!f) return -1;
  /* Master clocks since power-on. */
  for (i = 0; i < sor_watch_count; i++)
    fprintf(f, "%x %lld\n", sor_watch_log[i].pc,
            (long long)sor_watch_log[i].frame * SOR_CLOCKS_PER_FRAME + sor_watch_log[i].cycles - SOR_FIRST_FRAME_START);
  fclose(f); return 0;
}

void sor_ym_log(unsigned int cycles, unsigned int a, unsigned int v)
{
  if (!sor_ym_active || sor_ym_count >= sor_ym_cap) return;
  sor_ym_entries[sor_ym_count * 4] = sor_ym_frame_no; sor_ym_entries[sor_ym_count * 4 + 1] = cycles;
  sor_ym_entries[sor_ym_count * 4 + 2] = a; sor_ym_entries[sor_ym_count * 4 + 3] = v; sor_ym_count++;
}

__attribute__((visibility("default"))) void sor_ym_start(void)
{
  sor_ym_cap = 1u << 22; sor_ym_count = 0;
  sor_ym_entries = (unsigned int *)realloc(sor_ym_entries, sizeof(unsigned int) * 4 * sor_ym_cap); sor_ym_active = 1;
}

__attribute__((visibility("default"))) void sor_ym_frame(unsigned int frame) { sor_ym_frame_no = frame; }

__attribute__((visibility("default"))) int sor_ym_stop(const char *path)
{
  unsigned int i; FILE *f; sor_ym_active = 0; f = fopen(path, "w"); if (!f) return -1;
  for (i = 0; i < sor_ym_count; i++)
    fprintf(f, "%u %u %u %02x\n", sor_ym_entries[i * 4], sor_ym_entries[i * 4 + 1], sor_ym_entries[i * 4 + 2], sor_ym_entries[i * 4 + 3]);
  fclose(f); return 0;
}

/* Machine state at a frame end for state-synchronised comparisons with the
   native port: 68000 registers, work RAM, VDP (registers, VRAM, CRAM, VSRAM,
   control-port latches), Z80 RAM, bank and registers. Mega Drive byte order. */
extern unsigned char work_ram[0x10000], zram[0x2000], vram[0x10000], cram[0x80], vsram[0x80], reg[0x20], zstate;
extern unsigned int zbank;
extern void sor_vdp_latches(unsigned int *out);
static void sor_put32(FILE *f, unsigned int v) { fputc(v >> 24, f); fputc(v >> 16, f); fputc(v >> 8, f); fputc(v, f); }
static void sor_put16(FILE *f, unsigned int v) { fputc((v >> 8) & 0xff, f); fputc(v & 0xff, f); }

__attribute__((visibility("default"))) int sor_export_state(const char *path)
{
  unsigned int i, latches[3]; FILE *f = fopen(path, "wb"); if (!f) return -1;
  fwrite("SORSTAT1", 1, 8, f);
  for (i = 0; i < 16; i++) sor_put32(f, m68k_get_reg(M68K_REG_D0 + i));
  sor_put32(f, m68k_get_reg(M68K_REG_SR)); sor_put32(f, m68k_get_reg(M68K_REG_PC));
  for (i = 0; i < 0x10000; i++) fputc(work_ram[i ^ 1], f);
  for (i = 0; i < 0x10000; i++) fputc(vram[i ^ 1], f);
  for (i = 0; i < 64; i++) { unsigned int p = *(unsigned short *)&cram[i * 2];
    sor_put16(f, ((p & 0x1C0) << 3) | ((p & 0x038) << 2) | ((p & 0x007) << 1)); }
  for (i = 0; i < 40; i++) sor_put16(f, *(unsigned short *)&vsram[i * 2] & 0x7FF);
  fwrite(reg, 1, 24, f);
  sor_vdp_latches(latches); sor_put16(f, latches[0]); fputc(latches[1], f); fputc(latches[2], f);
  fwrite(zram, 1, 0x2000, f); sor_put32(f, zbank); fputc(zstate, f);
  sor_put16(f, Z80.pc.w.l); sor_put16(f, Z80.sp.w.l); sor_put16(f, Z80.af.w.l); sor_put16(f, Z80.bc.w.l);
  sor_put16(f, Z80.de.w.l); sor_put16(f, Z80.hl.w.l); sor_put16(f, Z80.ix.w.l); sor_put16(f, Z80.iy.w.l);
  sor_put16(f, Z80.af2.w.l); sor_put16(f, Z80.bc2.w.l); sor_put16(f, Z80.de2.w.l); sor_put16(f, Z80.hl2.w.l);
  fputc(Z80.i, f); fputc((Z80.r & 0x7f) | (Z80.r2 & 0x80), f); fputc(Z80.iff1, f); fputc(Z80.iff2, f); fputc(Z80.im, f); fputc(Z80.halt, f);
  fclose(f); return 0;
}
#endif
