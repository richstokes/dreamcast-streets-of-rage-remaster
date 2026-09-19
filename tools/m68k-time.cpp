// Measure MC68000 cycles of original ROM routines with the Musashi-derived
// core in the Genesis Plus GX research checkout (host tool only; never linked
// into the Dreamcast build). Each input line names a routine and its register
// inputs; the routine runs from a clean call until it returns.
//
// usage: m68k-time ROM < calls.txt
//   calls.txt lines: entry src dst d0   (hex; src->A0, dst->A1 and A4, d0->D0)
// output lines:      entry src cycles
// DRAM refresh restarts with each call (M68K_NO_REFRESH=1 disables it);
// M68K_PC_COUNTS=1 prints visits and cycles per instruction address to stderr.
// Work RAM starts zeroed except for the stack. The VDP ports accept writes and
// report "FIFO empty, not busy"; nothing else is emulated. Build: tools/m68k-time.sh
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "m68k.h"
int vdp_68k_irq_ack(int) { return -1; }
}

namespace {
uint8_t rom[0x80000], ram[0x10000];
unsigned vdp_read16(unsigned address) { return (address & 0x1c) == 4 ? 0x3400 : 0; }
unsigned vdp_read8(unsigned address) { return (vdp_read16(address & ~1u) >> ((address & 1) ? 0 : 8)) & 0xff; }
void ignore8(unsigned, unsigned) {}
void ignore16(unsigned, unsigned) {}
unsigned zero8(unsigned) { return 0; }
unsigned zero16(unsigned) { return 0; }
// Genesis Plus GX keeps 16-bit words byte-swapped on little-endian hosts.
void load_swapped(uint8_t *target, const uint8_t *source, size_t size) {
    for (size_t i = 0; i < size; i += 2) { target[i] = source[i + 1]; target[i + 1] = source[i]; }
}
}

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s ROM < calls\n", argv[0]); return 2; }
    std::vector<uint8_t> image(0x80000);
    FILE *f = std::fopen(argv[1], "rb");
    if (!f || std::fread(image.data(), 1, image.size(), f) != image.size()) { std::perror(argv[1]); return 1; }
    std::fclose(f);
    load_swapped(rom, image.data(), image.size());
    m68k_init();
    for (int bank = 0; bank < 256; bank++) {
        auto &map = m68k.memory_map[bank];
        map.base = nullptr; map.read8 = zero8; map.read16 = zero16; map.write8 = ignore8; map.write16 = ignore16;
        if (bank < 8) { map.base = rom + bank * 0x10000; map.read8 = nullptr; map.read16 = nullptr; }
        if (bank >= 0xe0) { map.base = ram; map.read8 = nullptr; map.read16 = nullptr; map.write8 = nullptr; map.write16 = nullptr; }
        if (bank == 0xc0) { map.read8 = vdp_read8; map.read16 = vdp_read16; }
    }
    const bool noRefresh = std::getenv("M68K_NO_REFRESH") != nullptr;
    const bool pcCounts = std::getenv("M68K_PC_COUNTS") != nullptr;   // per-PC visits to stderr
    constexpr unsigned kReturn = 0xFFFE00;   // RAM: BRA.S * marks the return
    std::vector<unsigned> visits(pcCounts ? 0x40000 : 0), spent(pcCounts ? 0x40000 : 0);
    // Run one call from entry to its RTS; returns CPU cycles, or -1 on timeout.
    const auto run = [&](unsigned entry, unsigned src, unsigned dst, unsigned d0) -> long {
        ram[(kReturn & 0xffff) ^ 1] = 0x60; ram[((kReturn & 0xffff) + 1) ^ 1] = 0xfe;
        m68k_pulse_reset();
        m68k_set_reg(M68K_REG_SR, 0x2700);
        // Push the return address, as JSR would.
        const unsigned sp = 0xFFFEFC;
        for (int i = 0; i < 4; i++) ram[((sp & 0xffff) + i) ^ 1] = uint8_t(kReturn >> (24 - 8 * i));
        m68k_set_reg(M68K_REG_SP, sp);
        m68k_set_reg(M68K_REG_A0, src); m68k_set_reg(M68K_REG_A1, dst); m68k_set_reg(M68K_REG_A4, dst);
        m68k_set_reg(M68K_REG_D0, d0);
        m68k_set_reg(M68K_REG_PC, entry);
        m68k.cycles = 0;
        // DRAM refresh (2 cycles per 128) restarts with each call; the core
        // otherwise keeps the previous call's refresh deadline.
        // M68K_NO_REFRESH=1 measures the instructions alone.
        m68k.refresh_cycles = noRefresh ? 0x7fffffff : 0;
        // One instruction per slice, so the count stops exactly at the return
        // (the final RTS is included, the BRA.S at the return point is not).
        for (unsigned target = 0; target < 400000000u;) {
            target = m68k.cycles + 1;
            const unsigned pc = m68k_get_reg(M68K_REG_PC), before = m68k.cycles;
            m68k_run(target);
            if (pcCounts && pc < 0x80000) { visits[pc >> 1]++; spent[pc >> 1] += (m68k.cycles - before) / 7; }
            if (m68k_get_reg(M68K_REG_PC) == kReturn) return long(m68k.cycles / 7);
        }
        return -1;
    };
    const auto ramWord = [&](unsigned address) { return unsigned(ram[(address & 0xffff) ^ 1] << 8 | ram[((address & 0xffff) + 1) ^ 1]); };
    unsigned entry, src, dst, d0;
    while (std::scanf("%x %x %x %x", &entry, &src, &dst, &d0) == 4) {
        std::memset(ram, 0, sizeof(ram));
        long cycles;
        if (entry == 0x84BA) {
            // Incremental Nemesis: queue src at $FFDCD0, start the stream, then
            // upload five tiles per call ($8510) until none remain.
            for (int i = 0; i < 4; i++) ram[((0xDCD0 + i) & 0xffff) ^ 1] = uint8_t(src >> (24 - 8 * i));
            cycles = run(0x84BA, 0, 0, 0);
            for (int calls = 0; cycles >= 0 && ramWord(0xDD28) != 0 && calls < 100000; calls++) {
                const long call = run(0x8510, 0, 0, 0);
                cycles = call < 0 ? -1 : cycles + call;
            }
        } else {
            cycles = run(entry, src, dst, d0);
        }
        std::printf("%x %x %ld%s\n", entry, src, cycles, cycles >= 0 ? "" : " timeout");
        for (unsigned i = 0; i < visits.size(); i++)
            if (visits[i]) { std::fprintf(stderr, "%x %u %u\n", i * 2, visits[i], spent[i]); visits[i] = spent[i] = 0; }
    }
}
