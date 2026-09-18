// Replay a SOR_YM_LOG chip-write log through pinned ymfm or Nuked OPN2 and
// write stereo s16 at the YM2612 sample rate to stdout. Host analysis only:
// Nuked OPN2 (LGPL-2.1, from the Genesis Plus GX research checkout) is never
// linked into the Dreamcast build. Build and run: tools/ym-compare.sh
//
// usage: ym-render <ymfm|nuked> log.bin first_sample count [channel]
// With a channel (0-5), key-ons and DAC data for every other channel are
// dropped; all other register writes are kept, isolating that channel.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include "ymfm_opn.h"
extern "C" {
#include "ym3438.h"
}

namespace {
struct Write { uint64_t sample; uint8_t port, value; };
std::vector<Write> load(const char *path) {
    std::vector<Write> out;
    FILE *f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    uint8_t r[10];
    while (std::fread(r, 1, 10, f) == 10) {
        uint64_t at = 0;
        for (int i = 0; i < 8; i++) at |= uint64_t(r[i]) << (i * 8);
        if (r[8] < 4) out.push_back({at, r[8], r[9]});
    }
    std::fclose(f);
    return out;
}
// Drop key-on/DAC writes that do not belong to the isolated channel.
std::vector<Write> isolate(const std::vector<Write> &in, int channel) {
    if (channel < 0) return in;
    std::vector<Write> out;
    uint8_t address[2]{};
    for (const auto &w : in) {
        if (!(w.port & 1)) { address[w.port >> 1] = w.value; out.push_back(w); continue; }
        if (w.port == 1 && address[0] == 0x28) {
            const int ch = (w.value & 3) + ((w.value & 4) ? 3 : 0);
            if ((w.value & 3) == 3 || ch != channel) continue;
        }
        if (w.port == 1 && address[0] == 0x2a && channel != 5) continue;
        out.push_back(w);
    }
    return out;
}
void put(int32_t l, int32_t r) {
    int16_t s[2] = {int16_t(std::max(-32768, std::min(32767, l))), int16_t(std::max(-32768, std::min(32767, r)))};
    std::fwrite(s, 2, 2, stdout);
}
}

int main(int argc, char **argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: %s <ymfm|nuked> log first count [channel]\n", argv[0]); return 2; }
    const bool nuked = !std::strcmp(argv[1], "nuked");
    const auto writes = isolate(load(argv[2]), argc > 5 ? std::atoi(argv[5]) : -1);
    const uint64_t first = std::strtoull(argv[3], nullptr, 10), count = std::strtoull(argv[4], nullptr, 10);
    size_t next = 0;
    if (!nuked) {
        ymfm::ymfm_interface intf;
        ymfm::ym2612 chip(intf);
        chip.reset();
        for (uint64_t s = 0; s < first + count; s++) {
            while (next < writes.size() && writes[next].sample <= s) { chip.write(writes[next].port, writes[next].value); next++; }
            ymfm::ym2612::output_data out;
            chip.generate(&out);
            if (s >= first) put(out.data[0], out.data[1]);
        }
        return 0;
    }
    static ym3438_t chip;
    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&chip);
    std::deque<Write> pending;   // one write per internal clock
    for (uint64_t s = 0; s < first + count; s++) {
        while (next < writes.size() && writes[next].sample <= s) pending.push_back(writes[next++]);
        int32_t l = 0, r = 0;
        for (int clock = 0; clock < 24; clock++) {
            if (!pending.empty()) { OPN2_Write(&chip, pending.front().port, pending.front().value); pending.pop_front(); }
            Bit16s buffer[2];
            OPN2_Clock(&chip, buffer);
            l += buffer[0]; r += buffer[1];
        }
        if (s >= first) put(l * 11, r * 11);   // Genesis Plus GX scaling
    }
}
