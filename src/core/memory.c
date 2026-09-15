#include "sor/memory.h"
#include <string.h>

void sor_memory_init(sor_memory *m, const uint8_t *rom, size_t size) {
    memset(m, 0, sizeof(*m));
    m->rom = rom;
    m->rom_size = rom ? size : 0;
}
static void fault(sor_memory *m, uint32_t a) {
    m->faults++;
    m->last_fault_address = a;
}
uint32_t sor_memory_read(sor_memory *m, uint32_t address, unsigned width) {
    uint32_t a = address & 0xffffffu, v = 0;
    if (width != 1 && width != 2 && width != 4) { fault(m, a); return 0; }
    if (a >= 0xff0000u) {
        /* Intentional WRAM wrapping, matching upstream, including odd offsets. */
        for (unsigned i = 0; i < width; ++i) v = (v << 8) | m->ram[(a + i) & 65535u];
        return v;
    }
    if (a < 0x400000u) {
        if (a >= m->rom_size || width > m->rom_size - a) { fault(m, a); return 0; }
        for (unsigned i = 0; i < width; ++i) v = (v << 8) | m->rom[a + i];
        return v;
    }
    if (m->read_device) return m->read_device(m->device, a, width);
    fault(m, a);
    return 0;
}
void sor_memory_write(sor_memory *m, uint32_t address, unsigned width, uint32_t value) {
    uint32_t a = address & 0xffffffu;
    if (width != 1 && width != 2 && width != 4) { fault(m, a); return; }
    if (a >= 0xff0000u) {
        for (unsigned i = 0; i < width; ++i)
            m->ram[(a + i) & 65535u] = (uint8_t)(value >> (8u * (width - 1 - i)));
        return;
    }
    if (a < 0x400000u) return; /* Cartridge is read-only. */
    if (m->write_device) m->write_device(m->device, a, width, value);
    else fault(m, a);
}
