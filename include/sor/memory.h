#ifndef SOR_MEMORY_H
#define SOR_MEMORY_H
#include <stddef.h>
#include <stdint.h>

/* Single simulation owner. No host pointers embedded in the emulated address space.
 * Unknown bus accesses are counted, never silently accepted as implemented devices.
 * Hardware word/long accesses preserve bus width (VDP data ports have side effects).
 */
typedef uint32_t (*sor_bus_read)(void *, uint32_t, unsigned);
typedef void (*sor_bus_write)(void *, uint32_t, unsigned, uint32_t);
typedef struct {
    const uint8_t *rom;
    size_t rom_size;
    uint8_t ram[65536];
    sor_bus_read read_device;
    sor_bus_write write_device;
    void *device;
    uint32_t faults;
    uint32_t last_fault_address;
} sor_memory;
void sor_memory_init(sor_memory *, const uint8_t *, size_t);
uint32_t sor_memory_read(sor_memory *, uint32_t address, unsigned width);
void sor_memory_write(sor_memory *, uint32_t address, unsigned width, uint32_t value);
#endif
