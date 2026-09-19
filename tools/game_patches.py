"""Checked adaptations of the pinned hand-written game functions.

The hand-written decompressors replace 68000 routines but cost no emulated CPU
time, so screen loads finished in a few frames instead of the original's dozens.
Each decoder now counts the events that dominate the original routine (codes,
nibbles, literals, copies, bit refills; tools/decoder_events.py) and charges
their fitted MC68000 cost after its writes, letting VBlanks occur and the VBlank
handler run (music continues through loads, as on the console). Costs come from
timing the ROM's own routines (docs/CADENCE.md).
"""

# MC68000 cycles per decode event, fitted by tools/fit-decoder-cycles.py to
# Musashi timings of the ROM's own routines. Held-out game decodes: Nemesis and
# Kosinski within 0.04 frame; Enigma (trained on game calls too) within 0.11.
COEFFICIENTS = {
    'nemesis': {'definitions': -2.227, 'table_entries': -3.315, 'groups': 160.118, 'codes': 147.314,
                'inline_': 142.343, 'nibbles': 50.948, 'rows': 6.083, 'xor_rows': 10.026, 'fetched': 45.702,
                'const': 10703.942},
    'kosinski': {'descriptor_bits': -61.593, 'descriptor_fetches': 513.637, 'literals': 99.641, 'short_': 324.412,
                 'short_bytes': 29.161, 'long_': 218.389, 'long_bytes': 39.638, 'extended': 264.026,
                 'extended_bytes': 32.640, 'const': -1211.920},
    'enigma': {'ops': -294.294, 'inline_words': 175.348, 'inline_bits': -79.389, 'words': 27.653,
               'words_increment': -2.154, 'words_common': 1.587, 'words_repeat': -17.522, 'words_up': 0.305,
               'words_down': -13.691, 'words_inline': 57.981, 'fetched': 671.887, 'const': -147.280},
}
# 68000 byte copy into Z80 RAM: move.b (a0)+,(a1)+ / dbf.
Z80_COPY = 22


def cycles_expression(family):
    terms = ' + '.join('%.3f * double(ev.%s)' % (v, k) for k, v in COEFFICIENTS[family].items() if k != 'const')
    return 'std::uint64_t(std::max(0.0, %s + %.3f))' % (terms, COEFFICIENTS[family]['const'])


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned game source changed; review cadence patch: ' + old[:60])
    return text.replace(old, new)


CHARGE = r'''
#include <algorithm>
// SoR port: spend emulated 68000 time in chunks, as translated code would.
#define SOR_CHARGE_CPU(cycles) do { \
    uint64_t sor_left_ = (cycles); \
    while (sor_left_ != 0) { \
        const unsigned sor_step_ = unsigned(sor_left_ < 2000 ? sor_left_ : 2000); \
        pace(sor_step_); \
        if (irqLevel() > cpu().interruptMask()) serviceIRQ(); \
        sor_left_ -= sor_step_; \
    } \
} while (0)
'''

EVENTS = '''struct DecodeEvents {
    std::uint64_t definitions{}, table_entries{}, groups{}, codes{}, inline_{}, nibbles{}, rows{}, xor_rows{}, fetched{};
    std::uint64_t descriptor_bits{}, descriptor_fetches{}, literals{}, short_{}, short_bytes{}, long_{}, long_bytes{},
                  extended{}, extended_bytes{};
    std::uint64_t ops{}, inline_words{}, inline_bits{}, words{}, words_increment{}, words_common{}, words_repeat{},
                  words_up{}, words_down{}, words_inline{};
};

struct DecodeResult {
    std::vector<m_byte> data;
    m_long              sourceEnd{};
    std::uint64_t       cycles{};   // SoR port: original MC68000 decode time
};'''


def patch(name, text):
    if name != 'SoRDecompress.cpp':
        return text
    edits = [
        ('#include "SoR.hpp"\n', '#include "SoR.hpp"\n' + CHARGE),
        ('''struct DecodeResult {
    std::vector<m_byte> data;
    m_long              sourceEnd{};
};''', EVENTS),
        # Bit readers count refills; the Kosinski descriptor reader counts bits and fetches.
        ('''            buffer_ = (buffer_ << 8) | reader_.readU8();
            bits_ += 8;
        }
    }
''', '''            buffer_ = (buffer_ << 8) | reader_.readU8();
            bits_ += 8;
            ++fetched_;
        }
    }

    public:
    std::uint64_t fetched_{};

    private:
'''),
        ('''    unsigned read() {
        const unsigned bit = descriptor_ & 1u;
        descriptor_ >>= 1;
        if (--remaining_ == 0) {''', '''    std::uint64_t bits_read{}, fetches{1};
    unsigned read() {
        ++bits_read;
        const unsigned bit = descriptor_ & 1u;
        descriptor_ >>= 1;
        if (--remaining_ == 0) {
            ++fetches;'''),
        # Nemesis
        ('    std::array<TableEntry, 256> table{};\n', '    std::array<TableEntry, 256> table{};\n    DecodeEvents ev;\n'),
        ('''    while (group != 0xFFu) {
        const m_byte pixel = group & 0x0Fu;''', '''    while (group != 0xFFu) {
        ++ev.groups;
        const m_byte pixel = group & 0x0Fu;'''),
        ('            const m_byte token = static_cast<m_byte>((descriptor & 0x70u) | pixel);\n',
         '            const m_byte token = static_cast<m_byte>((descriptor & 0x70u) | pixel);\n            ++ev.definitions;\n'),
        ('                table[index] = {static_cast<m_byte>(length), token, true};\n',
         '                table[index] = {static_cast<m_byte>(length), token, true};\n                ++ev.table_entries;\n'),
        ('            token = static_cast<m_byte>(bits.read(7));\n',
         '            token = static_cast<m_byte>(bits.read(7));\n            ++ev.inline_;\n'),
        ('            token = entry.token;\n', '            token = entry.token;\n            ++ev.codes;\n'),
        # Nibbles per code (not per iteration; at most 15 extra on the final row)
        # and rows from the output size keep the hot loop free of counters.
        ('        const m_byte value = token & 0x0Fu;\n', '        const m_byte value = token & 0x0Fu;\n        ev.nibbles += (token >> 4) + 1u;\n'),
        ('    NemesisResult result;\n', '    ev.fetched = bits.fetched_;\n    ev.rows = output.size() / 4;\n    ev.xor_rows = xorMode ? ev.rows : 0;\n    NemesisResult result;\n    result.cycles = %s;\n' % cycles_expression('nemesis')),
        # Enigma
        ('''    std::vector<m_byte> output;

    const auto inlineWord = [&]() {
        m_word                          word = baseTile;''', '''    std::vector<m_byte> output;
    DecodeEvents ev;

    const auto inlineWord = [&]() {
        ++ev.inline_words;
        m_word                          word = baseTile;'''),
        ('''            if ((attributeMask & maskBits[i]) == 0 || bits.read(1) == 0)
                continue;''', '''            if ((attributeMask & maskBits[i]) == 0)
                continue;
            ++ev.inline_bits;
            if (bits.read(1) == 0)
                continue;'''),
        ('''        if (indexBits != 0)
            word = static_cast<m_word>(word + bits.read(indexBits));''', '''        if (indexBits != 0) {
            ev.inline_bits += indexBits;
            word = static_cast<m_word>(word + bits.read(indexBits));
        }'''),
        ('''        const unsigned count  = bits.read(4);
        if (opcode == 7u && count == 0x0Fu) {
            if ((reader.position() & 1u) != 0)
                reader.readU8();
            return {std::move(output), reader.position()};
        }

        const unsigned run = count + 1u;''', '''        const unsigned count  = bits.read(4);
        ++ev.ops;
        if (opcode == 7u && count == 0x0Fu) {
            if ((reader.position() & 1u) != 0)
                reader.readU8();
            ev.fetched = bits.fetched_;
            return {std::move(output), reader.position(), %s};
        }

        const unsigned run = count + 1u;
        ev.words += run;
        switch (opcode) {
            case 0: ev.words_increment += run; break;
            case 1: ev.words_common += run; break;
            case 4: ev.words_repeat += run; break;
            case 5: ev.words_up += run; break;
            case 6: ev.words_down += run; break;
            default: ev.words_inline += run; break;
        }''' % cycles_expression('enigma')),
        # Kosinski
        ('''    KosinskiDescriptorReader descriptor(reader);
    std::vector<m_byte>      output;
''', '''    KosinskiDescriptorReader descriptor(reader);
    std::vector<m_byte>      output;
    DecodeEvents             ev;
'''),
        ('''            output.push_back(reader.readU8());
            continue;''', '''            output.push_back(reader.readU8());
            ++ev.literals;
            continue;'''),
        ('''            const unsigned length = ((descriptor.read() << 1) | descriptor.read()) + 2u;
''', '''            const unsigned length = ((descriptor.read() << 1) | descriptor.read()) + 2u;
            ++ev.short_;
            ev.short_bytes += length;
'''),
        ('''        if (lengthCode != 0) {
            copyMatch(displacement, lengthCode + 2u);''', '''        if (lengthCode != 0) {
            ++ev.long_;
            ev.long_bytes += lengthCode + 2u;
            copyMatch(displacement, lengthCode + 2u);'''),
        ('''        if (extension == 0)
            return {std::move(output), reader.position()};
        if (extension != 1)
            copyMatch(displacement, static_cast<unsigned>(extension) + 1u);''', '''        if (extension == 0) {
            ev.descriptor_bits = descriptor.bits_read;
            ev.descriptor_fetches = descriptor.fetches;
            return {std::move(output), reader.position(), %s};
        }
        if (extension != 1) {
            ++ev.extended;
            ev.extended_bytes += static_cast<unsigned>(extension) + 1u;
            copyMatch(displacement, static_cast<unsigned>(extension) + 1u);
        }''' % cycles_expression('kosinski')),
        # Charge each blocking decode its original time before the RTS.
        ('''    cpu().setNZClearVC(0, 0x8000u);
    cpu().ssp += 4;
}

// ---------------------------------------------------------------------------
// $82D2''', '''    cpu().setNZClearVC(0, 0x8000u);
    SOR_CHARGE_CPU(result.cycles);
    cpu().ssp += 4;
}

// ---------------------------------------------------------------------------
// $82D2'''),
        ('''    cpu().a[1] = destination + 8; // Saved by Enigma after the two header longs.
    cpu().ssp += 4;''', '''    cpu().a[1] = destination + 8; // Saved by Enigma after the two header longs.
    SOR_CHARGE_CPU(result.cycles);
    cpu().ssp += 4;'''),
        ('''    // A1 and D0-D7/A2-A5 are restored by the original MOVEM.
    cpu().ssp += 4;''', '''    // A1 and D0-D7/A2-A5 are restored by the original MOVEM.
    SOR_CHARGE_CPU(result.cycles);
    cpu().ssp += 4;'''),
        ('''    cpu().setDb(1, 0); // Terminator extension byte.
    cpu().setNZClearVC(0, 0x80u);
    cpu().ssp += 4;''', '''    cpu().setDb(1, 0); // Terminator extension byte.
    cpu().setNZClearVC(0, 0x80u);
    SOR_CHARGE_CPU(result.cycles);
    cpu().ssp += 4;'''),
        ('''    cpu().setDw(2, 0xFFFFu);
    cpu().setNZClearVC(0, 0x8000u);
    cpu().ssp += 4;''', '''    cpu().setDw(2, 0xFFFFu);
    cpu().setNZClearVC(0, 0x8000u);
    SOR_CHARGE_CPU(result.cycles + uint64_t(%d) * kZ80DriverCopyBytes);
    cpu().ssp += 4;''' % Z80_COPY),
    ]
    for old, new in edits:
        text = replace_once(text, old, new)
    return text
