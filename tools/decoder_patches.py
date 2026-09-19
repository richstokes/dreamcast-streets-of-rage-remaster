"""Emulated 68000 time for the hand-written decompressors (SoRDecompress.cpp).

The pinned port replaces the cartridge's Nemesis ($8192/$81A4, incremental
$84BA/$8510), Enigma ($82D6/$82D2) and Kosinski ($85A2) routines with host
code that costs no emulated time. Each decoder here adds the MC68000 time of
the ROM routine path by path, from its disassembly and the Musashi cycle table,
following the same decisions the ROM makes (including its bit-window refills).
tools/test-decoder-cycles.py checks every decode of a replay against
tools/m68k-time running the ROM's own routine. DRAM refresh (2 cycles per
~130 in these loops) is added when the time is charged.
"""


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned decoder source changed; review decoder_patches.py: ' + old[:60])
    return text.replace(old, new)


PRELUDE = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
// SoR port: spend the original decoder's 68000 time in chunks so VBlanks and
// the VBlank handler run during loads, as on the console. The time already
// includes DRAM refresh (sorRefresh), so it is charged without pace().
#define SOR_CHARGE_CPU(cycles, pc) do { \
    uint64_t sor_left_ = (cycles); \
    while (sor_left_ != 0) { \
        const unsigned sor_step_ = unsigned(sor_left_ < 2000 ? sor_left_ : 2000); \
        pcHistogram((pc), sor_step_); charge(sor_step_); \
        if (irqLevel() > cpu().interruptMask()) serviceIRQ(); \
        sor_left_ -= sor_step_; \
    } \
} while (0)

// Set around decodes the port adds that the original does not perform (the
// top-10 re-seed in restore_player_continues): they cost no emulated time.
bool sorUnchargedDecode = false;

namespace {
// DRAM refresh adds 2 cycles at the first instruction at least 128 cycles
// after the previous one: about one per 130-134 cycles in these loops
// (measured with tools/m68k-time for each decoder).
std::uint64_t sorRefresh(std::uint64_t cycles, unsigned period) {
    return cycles + cycles * 2 / period;
}

#ifdef SOR_PC_HISTOGRAM
// Host analysis: SOR_DECODE_LOG=1 lists each decode's ROM time before refresh
// (tools/test-decoder-cycles.py compares it with the ROM routine).
void sorLogDecode(m_long entry, m_long source, std::uint64_t cycles) {
    static const bool enabled = std::getenv("SOR_DECODE_LOG") != nullptr;
    if (enabled)
        std::fprintf(stderr, "DECODE entry=%lx src=%lx cycles=%llu\n", (unsigned long)entry, (unsigned long)source,
                     (unsigned long long)cycles);
}
#else
void sorLogDecode(m_long, m_long, std::uint64_t) {}
#endif
} // namespace
'''

# Nemesis decode loop ($81DC) and code table ($8280). d6 counts the unread bits
# of the ROM's 16-bit window; a code costs 6+2n for its shift, where n = d6-8.
NEMESIS_TIMING = r'''// SoR port: MC68000 time of the cartridge's Nemesis routines, path by path.
struct NemesisTiming {
    std::uint64_t cycles{};
    unsigned      d6 = 16;   // unread bits in the ROM's 16-bit window
    bool          xorMode{};

    unsigned refill(unsigned kept, unsigned loaded) {
        if (d6 >= 9)
            return kept;
        d6 += 8;
        return loaded;
    }
    // $8280: the code table.
    void tableStart(bool empty) { cycles += empty ? 40 : 26; }
    void group() { cycles += 4; }
    void descriptor(m_byte value) { cycles += 16 + (value < 0x80u ? 0 : value == 0xFFu ? 42 : 28); }
    void definition(unsigned length) {
        const unsigned k = 8 - length;
        cycles += 78 + (k == 0 ? 44 : 56 + 4 * k + 28 * (1u << k));
    }
    // $81DC: a table code, or the 6-bit escape and a 7-bit inline code.
    void code(unsigned length) {
        cycles += 76 + 2 * (d6 - 8);
        d6 -= length;
        cycles += refill(10, 42) + 48;
    }
    void inlineCode() {
        cycles += 48 + 2 * (d6 - 8);
        d6 -= 6;
        cycles += refill(10, 42);
        d6 -= 7;
        cycles += 42 + 2 * d6;
        cycles += refill(10, 52) + 14;
    }
    // Each nibble; a completed row goes through the writer, which either
    // continues ($821E) or returns after the last row.
    void nibble() { cycles += 34; }
    void rowEnd() { cycles += 40 + (xorMode ? 8 : 0); }
    void rowWritten() { cycles += 42; }
    void lastRow() { cycles += 48; }
    // DBF over the run, and BRA.S to the next code when the run ends.
    void next(bool moreInRun) { cycles += moreInRun ? 10 : 24; }
};

'''

STREAM = r'''// SoR port: the incremental queue decodes each tile when it is uploaded ($8510),
// with the ROM's per-tile time. The queue state is host-owned.
struct NemesisStream {
    struct Entry {
        m_byte length{};
        m_byte token{};
        bool   valid{};
    };
    std::array<Entry, 256> table{};
    m_long        position{};
    std::uint64_t buffer{};
    unsigned      bits{};
    m_long        previousRow{};
    m_byte        runValue{};
    unsigned      runLeft{};
    NemesisTiming timing;

    template <typename ReadByte> unsigned read(ReadByte &readByte, unsigned count, bool consume = true) {
        while (bits < count) {
            buffer = (buffer << 8) | readByte(position++);
            bits += 8;
        }
        const unsigned value = unsigned((buffer >> (bits - count)) & ((std::uint64_t{1} << count) - 1));
        if (consume) {
            bits -= count;
            buffer &= (std::uint64_t{1} << bits) - 1;
        }
        return value;
    }

    // Decode the next tile into out; returns its MC68000 time: from $821E,
    // where the previous tile's writer returned inside a run, to this tile's.
    template <typename ReadByte> std::uint64_t tile(ReadByte &readByte, m_byte *out) {
        const std::uint64_t start = timing.cycles;
        timing.cycles += 8;
        timing.next(runLeft != 0);
        for (unsigned rowIndex = 0; rowIndex < 8; ++rowIndex) {
            m_long row{};
            for (unsigned nibble = 0; nibble < 8; ++nibble) {
                if (runLeft == 0) {
                    m_byte token{};
                    const unsigned prefix = read(readByte, 8, false);
                    if (prefix >= 0xFCu) {
                        timing.inlineCode();
                        read(readByte, 6);
                        token = static_cast<m_byte>(read(readByte, 7));
                    } else {
                        const Entry entry = table[prefix];
                        if (!entry.valid)
                            throw std::runtime_error("undefined Nemesis prefix code");
                        timing.code(entry.length);
                        read(readByte, entry.length);
                        token = entry.token;
                    }
                    runValue = token & 0x0Fu;
                    runLeft  = (token >> 4) + 1u;
                }
                row = (row << 4) | runValue;
                --runLeft;
                if (nibble != 7) {
                    timing.nibble();
                    timing.next(runLeft != 0);
                    continue;
                }
                timing.rowEnd();
                if (rowIndex == 7) {
                    timing.lastRow();
                } else {
                    timing.rowWritten();
                    timing.next(runLeft != 0);
                }
            }
            if (timing.xorMode) {
                row ^= previousRow;
                previousRow = row;
            }
            for (unsigned byte = 0; byte < 4; ++byte)
                out[rowIndex * 4 + byte] = static_cast<m_byte>(row >> (24 - 8 * byte));
        }
        return timing.cycles - start;
    }
};

'''


def edits():
    return [
        ('#include "SoR.hpp"\n', '#include "SoR.hpp"\n' + PRELUDE),
        ('''struct DecodeResult {
    std::vector<m_byte> data;
    m_long              sourceEnd{};
};''', '''struct DecodeResult {
    std::vector<m_byte> data;
    m_long              sourceEnd{};
    std::uint64_t       cycles{};   // SoR port: MC68000 time of the ROM routine, before refresh
};'''),
        ('''struct NemesisResult : DecodeResult {
    m_word tileCount{};''', '''struct NemesisResult : DecodeResult {
    std::uint64_t tableCycles{};   // SoR port: the code table's share of cycles
    m_word tileCount{};'''),
        # The Kosinski descriptor reader counts its refills.
        ('''    unsigned read() {
        const unsigned bit = descriptor_ & 1u;
        descriptor_ >>= 1;
        if (--remaining_ == 0) {''', '''    std::uint64_t refills{};   // SoR port: for the ROM's time
    unsigned read() {
        const unsigned bit = descriptor_ & 1u;
        descriptor_ >>= 1;
        if (--remaining_ == 0) {
            ++refills;'''),

        # ---- Nemesis: table build and decode loop ----
        ('template <typename ReadByte> NemesisResult decodeNemesis(ReadByte readByte, m_long source) {\n',
         NEMESIS_TIMING + STREAM +
         'template <typename ReadByte> NemesisResult decodeNemesis(ReadByte readByte, m_long source, NemesisStream *stream = nullptr) {\n'),
        ('''    std::array<TableEntry, 256> table{};

    m_byte group = reader.readU8();
    while (group != 0xFFu) {
        const m_byte pixel = group & 0x0Fu;
        for (;;) {
            const m_byte descriptor = reader.readU8();
            if (descriptor >= 0x80u) {''', '''    std::array<TableEntry, 256> table{};
    NemesisTiming timing;
    timing.xorMode = xorMode;

    m_byte group = reader.readU8();
    timing.tableStart(group == 0xFFu);
    while (group != 0xFFu) {
        timing.group();
        const m_byte pixel = group & 0x0Fu;
        for (;;) {
            const m_byte descriptor = reader.readU8();
            timing.descriptor(descriptor);
            if (descriptor >= 0x80u) {'''),
        ('''            const m_byte token = static_cast<m_byte>((descriptor & 0x70u) | pixel);
''', '''            const m_byte token = static_cast<m_byte>((descriptor & 0x70u) | pixel);
            timing.definition(length);
'''),
        ('    MsbBitReader bits(reader, initialBits, 16);\n', '''    const std::uint64_t tableCycles = timing.cycles;
    if (stream != nullptr) {
        // SoR port: incremental queue. Hand the table and bit position over.
        for (unsigned index = 0; index < 256; ++index)
            stream->table[index] = {table[index].length, table[index].token, table[index].valid};
        stream->position       = initialPayloadCursor;
        stream->buffer         = initialBits;
        stream->bits           = 16;
        stream->timing.xorMode = xorMode;
        NemesisResult result;
        result.tableCycles          = tableCycles;
        result.tileCount            = tileCount;
        result.initialBitBuffer     = initialBits;
        result.initialPayloadCursor = initialPayloadCursor;
        result.xorMode              = xorMode;
        return result;
    }
    MsbBitReader bits(reader, initialBits, 16);
'''),
        ('''            bits.read(6);
            token = static_cast<m_byte>(bits.read(7));
''', '''            timing.inlineCode();
            bits.read(6);
            token = static_cast<m_byte>(bits.read(7));
'''),
        ('''            bits.read(entry.length);
            token = entry.token;
''', '''            timing.code(entry.length);
            bits.read(entry.length);
            token = entry.token;
'''),
        ('''            row = (row << 4) | value;
            if (++nibbles != 8)
                continue;
''', '''            row = (row << 4) | value;
            if (++nibbles != 8) {
                timing.nibble();
                timing.next(repeat != 1);
                continue;
            }
'''),
        ('''            appendLong(output, row);
            --rowsRemaining;
            row     = 0;
            nibbles = 0;
            if (rowsRemaining == 0)
                break;
''', '''            appendLong(output, row);
            timing.rowEnd();
            --rowsRemaining;
            row     = 0;
            nibbles = 0;
            if (rowsRemaining == 0) {
                timing.lastRow();
                break;
            }
            timing.rowWritten();
            timing.next(repeat != 1);
'''),
        ('''    NemesisResult result;
    result.data                 = std::move(output);''', '''    NemesisResult result;
    result.cycles               = timing.cycles;
    result.tableCycles          = tableCycles;
    result.data                 = std::move(output);'''),

        # ---- Enigma ($82D6): d6 counts the unread bits of the ROM's window ----
        ('''    MsbBitReader bits(reader);

    std::vector<m_byte> output;

    const auto inlineWord = [&]() {
        m_word                          word = baseTile;''', '''    MsbBitReader bits(reader);

    std::vector<m_byte> output;
    // SoR port: ROM time. romCursor follows the ROM's A0 for its final alignment.
    std::uint64_t cycles = 226;
    unsigned      d6        = 16;
    m_long        romCursor = source + 8;
    const auto refill = [&](unsigned kept, unsigned loaded) -> unsigned {
        if (d6 >= 9)
            return kept;
        d6 += 8;
        ++romCursor;
        return loaded;
    };

    const auto inlineWord = [&]() {
        cycles += 8;
        m_word                          word = baseTile;'''),
        ('''            if ((attributeMask & maskBits[i]) == 0 || bits.read(1) == 0)
                continue;''', '''            if ((attributeMask & maskBits[i]) == 0) {
                cycles += 14;
                continue;
            }
            --d6;
            if (bits.read(1) == 0) {
                cycles += 32;
                continue;
            }
            cycles += 38;'''),
        ('''        if (indexBits != 0)
            word = static_cast<m_word>(word + bits.read(indexBits));''', '''        if (indexBits != 0)
            word = static_cast<m_word>(word + bits.read(indexBits));
        const int spare = int(d6) - int(indexBits);
        if (spare > 0) {
            cycles += 104 + 2 * unsigned(spare);
            d6 = unsigned(spare);
            cycles += refill(10, 42);
        } else if (spare == 0) {
            cycles += 126;
            d6 = 16;
            romCursor += 2;
        } else {
            cycles += 158 + 4 * unsigned(-spare);
            d6 = unsigned(16 + spare);
            romCursor += 2;
        }'''),
        ('''        const unsigned count  = bits.read(4);
        if (opcode == 7u && count == 0x0Fu) {
            if ((reader.position() & 1u) != 0)
                reader.readU8();
            return {std::move(output), reader.position()};
        }

        const unsigned run = count + 1u;''', '''        const unsigned count  = bits.read(4);
        const unsigned used   = opcode >= 4u ? 7u : 6u;
        cycles += 138 + 2 * (d6 - 7) + (used == 7u ? 10 : 20);
        d6 -= used;
        cycles += refill(10, 42);
        if (opcode == 7u && count == 0x0Fu) {
            if ((reader.position() & 1u) != 0)
                reader.readU8();
            cycles += 34 + (d6 != 16 ? 10 : 16);
            const m_long romEnd = romCursor - 1 - (d6 == 16 ? 1 : 0);
            cycles += 12 + ((romEnd & 1u) == 0 ? 10 : 16) + 132;
            return {std::move(output), reader.position(), cycles};
        }

        const unsigned run = count + 1u;
        // Increment, common, (unused), repeat, up, down, inline words.
        constexpr unsigned perWord[8] = {26, 18, 0, 0, 18, 22, 22, 36};
        constexpr unsigned perRun[8]  = {14, 14, 0, 0, 32, 32, 32, 30};
        cycles += perWord[opcode] * run + perRun[opcode];'''),

        # ---- Kosinski ($85A2) ----
        ('''    KosinskiDescriptorReader descriptor(reader);
    std::vector<m_byte>      output;
''', '''    KosinskiDescriptorReader descriptor(reader);
    std::vector<m_byte>      output;
    std::uint64_t            cycles = 48 + 200;   // SoR port: entry and terminator
'''),
        ('''            output.push_back(reader.readU8());
            continue;''', '''            output.push_back(reader.readU8());
            cycles += 66;
            continue;'''),
        ('''            const unsigned length = ((descriptor.read() << 1) | descriptor.read()) + 2u;
''', '''            const unsigned length = ((descriptor.read() << 1) | descriptor.read()) + 2u;
            cycles += 186 + 32 * length;
'''),
        ('''        if (lengthCode != 0) {
            copyMatch(displacement, lengthCode + 2u);''', '''        if (lengthCode != 0) {
            cycles += 178 + 32 * (lengthCode + 2u);
            copyMatch(displacement, lengthCode + 2u);'''),
        ('''        if (extension == 0)
            return {std::move(output), reader.position()};
        if (extension != 1)
            copyMatch(displacement, static_cast<unsigned>(extension) + 1u);''', '''        if (extension == 0)
            return {std::move(output), reader.position(), cycles + 44 * descriptor.refills};
        if (extension == 1) {
            cycles += 192;
        } else {
            cycles += 222 + 32 * (static_cast<unsigned>(extension) + 1u);
            copyMatch(displacement, static_cast<unsigned>(extension) + 1u);
        }'''),

        # ---- Incremental Nemesis ($84BA begin, $8510 continue) ----
        ('''struct IncrementalNemesisState {
    std::vector<m_byte> decoded;
    std::size_t         uploadedTiles{};
    m_long              sourceEnd{};
    bool                xorMode{};
};''', '''struct IncrementalNemesisState {
    NemesisStream stream;   // SoR port: decoded tile by tile as uploaded
    std::size_t   uploadedTiles{};
    bool          xorMode{};
    m_long        source{};
};'''),
        ('''    const m_long source = memory().readLong(kArtQueue);
    if (source == 0) {
        if (memory().readWord(kIncrementalTiles) == 0)
            incrementalNemesis.erase(this);
        cpu().ssp += 4;
        return;
    }
    if (memory().readWord(kIncrementalTiles) != 0) {
        cpu().ssp += 4;
        return;
    }''', '''    const m_long source = memory().readLong(kArtQueue);
    if (source == 0) {
        if (memory().readWord(kIncrementalTiles) == 0)
            incrementalNemesis.erase(this);
        SOR_CHARGE_CPU(sorRefresh(42, 133), 0x84BAu);
        cpu().ssp += 4;
        return;
    }
    if (memory().readWord(kIncrementalTiles) != 0) {
        SOR_CHARGE_CPU(sorRefresh(62, 133), 0x84BAu);
        cpu().ssp += 4;
        return;
    }'''),
        ('''    auto result = decodeNemesis(readByte, source);

    IncrementalNemesisState state;
    state.decoded            = std::move(result.data);
    state.sourceEnd          = result.sourceEnd;
    state.xorMode            = result.xorMode;''', '''    IncrementalNemesisState state;
    auto result = decodeNemesis(readByte, source, &state.stream);
    state.xorMode            = result.xorMode;
    state.source             = source;'''),
        ('''    cpu().d[0] = 0;
    cpu().setNZClearVC(cpu().d[6], 0x80000000u);
    cpu().ssp += 4;''', '''    cpu().d[0] = 0;
    cpu().setNZClearVC(cpu().d[6], 0x80000000u);
    const std::uint64_t sorCycles = 300 + (result.xorMode ? 20 : 10) + result.tableCycles;
    sorLogDecode(0x84BAu, source, sorCycles);
    SOR_CHARGE_CPU(sorRefresh(sorCycles, 133), 0x84BAu);
    cpu().ssp += 4;'''),
        ('''    m_word remaining = memory().readWord(kIncrementalTiles);
    if (remaining == 0) {
        cpu().ssp += 4;
        return;
    }''', '''    m_word remaining = memory().readWord(kIncrementalTiles);
    if (remaining == 0) {
        SOR_CHARGE_CPU(sorRefresh(38, 133), 0x8510u);
        cpu().ssp += 4;
        return;
    }'''),
        ('''    unsigned uploadedThisCall = 0;
''', '''    unsigned uploadedThisCall = 0;
    std::uint64_t sorCycles = 250;   // SoR port: $8510 time for this call
    const auto readByte = [this](m_long address) {
        return memory().readByte(address);
    };
'''),
        ('''        const std::size_t tileOffset = state.uploadedTiles * 32u;
        if (tileOffset + 32u > state.decoded.size())
            throw std::runtime_error("incremental Nemesis tile count exceeds host output");

        for (std::size_t offset = tileOffset; offset < tileOffset + 32u; offset += 2) {
            const m_word word =
                static_cast<m_word>((static_cast<m_word>(state.decoded[offset]) << 8) | state.decoded[offset + 1]);
            vdp().writeDataPort(word);
        }
''', '''        m_byte tile[32];
        sorCycles += 8 + 18 + state.stream.tile(readByte, tile) + 16;
        sorCycles += remaining == 1 ? 10 : uploadedThisCall == 4 ? 32 : 34;
        for (std::size_t offset = 0; offset < 32u; offset += 2)
            vdp().writeDataPort(static_cast<m_word>((static_cast<m_word>(tile[offset]) << 8) | tile[offset + 1]));
'''),
        ('''    if (state.xorMode && state.uploadedTiles != 0) {
        const std::size_t lastRow = state.uploadedTiles * 32u - 4u;
        const m_long      row     = (static_cast<m_long>(state.decoded[lastRow]) << 24) |
                                    (static_cast<m_long>(state.decoded[lastRow + 1]) << 16) |
                                    (static_cast<m_long>(state.decoded[lastRow + 2]) << 8) | state.decoded[lastRow + 3];
        memory().writeLong(kIncrementalXorRow, row);
    }''', '''    if (state.xorMode && state.uploadedTiles != 0)
        memory().writeLong(kIncrementalXorRow, state.stream.previousRow);'''),
        ('''        // The decoder's bit-level state is host-owned. Keep the RAM cursor
        // nonzero for queue producers and diagnostics while the stream is live.
        memory().writeLong(kArtQueue, state.sourceEnd);
        cpu().ssp += 4;
        return;''', '''        // The decoder's bit-level state is host-owned. Store the source
        // cursor, as the original stores A0, so the queue head stays nonzero.
        memory().writeLong(kArtQueue, state.stream.position);
        sorCycles += 148;
        sorLogDecode(0x8510u, state.source, sorCycles);
        SOR_CHARGE_CPU(sorRefresh(sorCycles, 133), 0x8510u);
        cpu().ssp += 4;
        return;'''),
        ('''    incrementalNemesis.erase(stateIt);
    cpu().ssp += 4;''', '''    sorCycles += 440;
    sorLogDecode(0x8510u, state.source, sorCycles);
    incrementalNemesis.erase(stateIt);
    SOR_CHARGE_CPU(sorRefresh(sorCycles, 133), 0x8510u);
    cpu().ssp += 4;'''),

        # ---- Charges at each blocking entry, before the RTS ----
        # $8192 (VDP) / $81A4 (RAM): MOVEM and setup, table and decode loop, MOVEM and RTS.
        ('''    cpu().setNZClearVC(0, 0x8000u);
    cpu().ssp += 4;
}

// ---------------------------------------------------------------------------
// $82D2''', '''    cpu().setNZClearVC(0, 0x8000u);
    {
        const std::uint64_t sorCycles = (ramDestination ? 124 : 146) + (result.xorMode ? 88 : 78) + 64 + 132 + result.cycles;
        sorLogDecode(ramDestination ? 0x81A4u : 0x8192u, source, sorCycles);
        SOR_CHARGE_CPU(sorRefresh(sorCycles, 133), 0x8192u);
    }
    cpu().ssp += 4;
}

// ---------------------------------------------------------------------------
// $82D2'''),
        ('''    auto result = decodeNemesis(readByte, cpu().a[0]);
''', '''    const m_long source = cpu().a[0];
    auto result = decodeNemesis(readByte, source);
'''),
        # $82D2 copies the two header longs first.
        ('''    cpu().a[1] = destination + 8; // Saved by Enigma after the two header longs.
    cpu().ssp += 4;''', '''    cpu().a[1] = destination + 8; // Saved by Enigma after the two header longs.
    sorLogDecode(0x82D2u, source, 40 + result.cycles);
    SOR_CHARGE_CPU(sorRefresh(40 + result.cycles, 134), 0x82D6u);
    cpu().ssp += 4;'''),
        # $82D6 and its wrappers: $12832 (moveq/lea/lea/jmp, 32) and $112C0
        # (level 3: 114 before the decode; otherwise 56 and no decode).
        ('''            memory().writeByte(kLevel3AnimationFlag, 0);
            cpu().setNZClearVC(0, 0x80u);
            cpu().ssp += 4;
            return;''', '''            memory().writeByte(kLevel3AnimationFlag, 0);
            cpu().setNZClearVC(0, 0x80u);
            SOR_CHARGE_CPU(sorRefresh(56, 134), 0x82D6u);
            cpu().ssp += 4;
            return;'''),
        ('''    const m_long destination = cpu().a[1];
    auto         result      = decodeEnigma(readByte, cpu().a[0], cpu().dw(0));''', '''    const m_long destination = cpu().a[1];
    const m_long source      = cpu().a[0];
    auto         result      = decodeEnigma(readByte, cpu().a[0], cpu().dw(0));'''),
        ('''    // A1 and D0-D7/A2-A5 are restored by the original MOVEM.
    cpu().ssp += 4;''', '''    // A1 and D0-D7/A2-A5 are restored by the original MOVEM.
    {
        const std::uint64_t sorCycles = result.cycles + (entry_ == 0x00012832u ? 32 : entry_ == 0x000112C0u ? 114 : 0);
        sorLogDecode(entry_ == 0x00012832u || entry_ == 0x000112C0u ? entry_ : 0x82D6u, source, sorCycles);
        if (!sorUnchargedDecode)
            SOR_CHARGE_CPU(sorRefresh(sorCycles, 134), 0x82D6u);
    }
    cpu().ssp += 4;'''),
        ('''    cpu().setDb(1, 0); // Terminator extension byte.
    cpu().setNZClearVC(0, 0x80u);
    cpu().ssp += 4;''', '''    cpu().setDb(1, 0); // Terminator extension byte.
    cpu().setNZClearVC(0, 0x80u);
    sorLogDecode(0x85A2u, source, result.cycles);
    SOR_CHARGE_CPU(sorRefresh(result.cycles, 130), 0x85A2u);
    cpu().ssp += 4;'''),
        ('''    const m_long destination = cpu().a[1];
    auto         result      = decodeKosinski(readByte, cpu().a[0]);''', '''    const m_long destination = cpu().a[1];
    const m_long source      = cpu().a[0];
    auto         result      = decodeKosinski(readByte, cpu().a[0]);'''),
        # $1061C decodes the driver into work RAM at $FF7000 and copies it from
        # there; upstream decodes in host memory. Leave the RAM image the ROM
        # leaves (the buffer keeps it until the next load overwrites it).
        ('''    for (std::size_t offset = 0; offset < kZ80DriverCopyBytes; ++offset)
        z80().writeRAMFor68K(''', '''    for (std::size_t offset = 0; offset < result.data.size(); ++offset)
        memory().writeByte(0x00FF7000u + static_cast<m_long>(offset), result.data[offset]);
    for (std::size_t offset = 0; offset < kZ80DriverCopyBytes; ++offset)
        z80().writeRAMFor68K('''),
        # $1061C: bus requests (116), the Kosinski decode, setup (36), the copy of
        # 0x1EC7 bytes into Z80 RAM (13 per byte with the Z80-bus wait, DBF 10/14),
        # the sample bank bytes, Z80 reset pulse and RTS (162).
        ('''    cpu().setDw(2, 0xFFFFu);
    cpu().setNZClearVC(0, 0x8000u);
    cpu().ssp += 4;''', '''    cpu().setDw(2, 0xFFFFu);
    cpu().setNZClearVC(0, 0x8000u);
    {
        const std::uint64_t copy = 13 * kZ80DriverCopyBytes + 10 * (kZ80DriverCopyBytes - 1) + 14;
        sorLogDecode(0x1061Cu, kZ80DacDriverSource, result.cycles);
        SOR_CHARGE_CPU(sorRefresh(116 + result.cycles + 36 + copy + 162, 130), 0x1061Cu);
    }
    cpu().ssp += 4;'''),
    ]


def patch(text):
    for old, new in edits():
        text = replace_once(text, old, new)
    return text
