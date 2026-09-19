/* SoR port analysis build: per-PC 68000 cycle histogram (not part of the game). */
#ifdef HOOK_CPU
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../m68k/m68k.h"
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
#endif
#ifdef HOOK_CPU
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
  /* Master clocks since power-on: frames of 896040 plus the frame's clock; the
     68000 starts 784,184 clocks into the first emulated frame (genesis.c). */
  for (i = 0; i < sor_watch_count; i++)
    fprintf(f, "%x %lld\n", sor_watch_log[i].pc, (long long)sor_watch_log[i].frame * 896040 + sor_watch_log[i].cycles - 784184);
  fclose(f); return 0;
}
#endif
