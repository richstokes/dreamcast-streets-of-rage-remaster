/* SoR port analysis build: per-PC 68000 cycle histogram (not part of the game). */
#ifdef HOOK_CPU
#include <string.h>
#include "../m68k/m68k.h"
static unsigned long long sor_pc_cycles[0x40000];
static unsigned int sor_last_pc;
static int sor_last_cycles, sor_active;
static void sor_hook(hook_type_t type, int width, unsigned int address, unsigned int value)
{
  (void)width; (void)value;
  if (type != HOOK_M68K_E) return;
  if (sor_active && sor_last_pc < 0x80000) {
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
