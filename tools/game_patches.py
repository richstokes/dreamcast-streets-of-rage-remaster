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


def table_cycles_expression():
    c = COEFFICIENTS['nemesis']
    return 'std::uint64_t(std::max(0.0, %.3f * double(ev.definitions) + %.3f * double(ev.table_entries) + %.3f * double(ev.groups) + %.3f))' % (
        c['definitions'], c['table_entries'], c['groups'], c['const'])


def tile_expression():
    c = COEFFICIENTS['nemesis']
    return ('std::max(0.0, %.3f * double(codes - codes0) + %.3f * double(inline_ - inline0) + '
            '%.3f * double(nibbles - nibbles0) + %.3f * 8.0 + %.3f * (xorMode ? 8.0 : 0.0) + '
            '%.3f * double(fetched - fetched0))') % (
        c['codes'], c['inline_'], c['nibbles'], c['rows'], c['xor_rows'], c['fetched'])


# Resumable Nemesis decoder for the incremental queue ($8510): one tile at a
# time with the blocking decoder's code rules and per-tile cost events.
STREAM = r"""// SoR port: incremental Nemesis stream, decoded one tile per upload.
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
    bool          xorMode{};
    m_long        previousRow{};
    m_byte        runValue{};
    unsigned      runLeft{};
    std::uint64_t codes{}, inline_{}, nibbles{}, fetched{};

    template <typename ReadByte> unsigned read(ReadByte &readByte, unsigned count, bool consume = true) {
        while (bits < count) {
            buffer = (buffer << 8) | readByte(position++);
            bits += 8;
            ++fetched;
        }
        const unsigned value = unsigned((buffer >> (bits - count)) & ((std::uint64_t{1} << count) - 1));
        if (consume) {
            bits -= count;
            buffer &= (std::uint64_t{1} << bits) - 1;
        }
        return value;
    }

    // Decode the next tile into out; returns its MC68000 decode time.
    template <typename ReadByte> std::uint32_t tile(ReadByte &readByte, m_byte *out) {
        const std::uint64_t codes0 = codes, inline0 = inline_, nibbles0 = nibbles, fetched0 = fetched;
        for (unsigned rowIndex = 0; rowIndex < 8; ++rowIndex) {
            m_long row{};
            for (unsigned nibble = 0; nibble < 8; ++nibble) {
                if (runLeft == 0) {
                    m_byte token{};
                    const unsigned prefix = read(readByte, 8, false);
                    if (prefix >= 0xFCu) {
                        read(readByte, 6);
                        token = static_cast<m_byte>(read(readByte, 7));
                        ++inline_;
                    } else {
                        const Entry entry = table[prefix];
                        if (!entry.valid)
                            throw std::runtime_error("undefined Nemesis prefix code");
                        read(readByte, entry.length);
                        token = entry.token;
                        ++codes;
                    }
                    runValue = token & 0x0Fu;
                    runLeft  = (token >> 4) + 1u;
                    nibbles += runLeft;
                }
                row = (row << 4) | runValue;
                --runLeft;
            }
            if (xorMode) {
                row ^= previousRow;
                previousRow = row;
            }
            for (unsigned byte = 0; byte < 4; ++byte)
                out[rowIndex * 4 + byte] = static_cast<m_byte>(row >> (24 - 8 * byte));
        }
        return std::uint32_t(%s);
    }
};

""" % tile_expression()


# $8510 outside the shared decode loop, per call that uploads tiles (Genesis
# Plus GX profile of the Round 1 replay, build/prof-lag.bin).
INCREMENTAL_CALL = 700


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
        pcHistogram(0x8192u, sor_step_); pace(sor_step_); \
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
    std::uint64_t       tableCycles{}; // Nemesis: the code-table build within cycles
};'''


# Hand-written routines charged their mean cost per call (Genesis Plus GX
# profile of 3,000 Round 1 frames): file -> (entry line, ROM address, cycles).
ENTRY_CHARGES = {
    'SoRControls.cpp': [('    traceEnter(0x568Au);\n', 0x568A, 125)],                 # remap_player_gameplay_input
    'SoRManualFunctions.cpp': [('    traceEnter(0x41EAu);\n', 0x41EA, 229)],          # compute_player_attack_descriptor
    'SoRSound.cpp': [('    traceEnter(0x0001069Eu);\n', 0x1069E, 197)],               # queue_sound_id
}


def charge_entries(name, text):
    for line, address, cycles in ENTRY_CHARGES.get(name, []):
        text = replace_once(text, line, line + '    pcHistogram(0x%Xu, %d); pace(%d);\n' % (address, cycles, cycles))
    return text


def patch(name, text):
    text = charge_entries(name, text)
    if name == 'SoRSound.cpp':
        # sound_ym2612_acquire ($73298): move.w #$100,BUSREQ (20); btst/bne on
        # the grant (28); btst #7,($A01FFD) (20); while the DAC driver is busy,
        # release (8+20) and retry after three NOPs and bra.s (22); otherwise
        # beq.s (10), then poll YM busy with move.b/btst/bne.s (34) and rts (16).
        # The busy flag comes from the shadow DAC driver (NativeAudio).
        text = replace_once(text, '''    while (!shouldQuit()) {
        memory().writeWord(kZ80Busreq, 0x0100u);
        memory().waitForByteValue(kZ80Busreq, 0, waitForHardware);''', '''    while (!shouldQuit()) {
        pcHistogram(0x73298u, 20); pace(20);
        memory().writeWord(kZ80Busreq, 0x0100u);
        pcHistogram(0x73298u, 48); pace(48);
        memory().waitForByteValue(kZ80Busreq, 0, waitForHardware);''')
        text = replace_once(text, '''            memory().writeWord(kZ80Busreq, 0);
            continue;''', '''            pcHistogram(0x73298u, 28); pace(28);
            memory().writeWord(kZ80Busreq, 0);
            pcHistogram(0x73298u, 22); pace(22);
            continue;''')
        return replace_once(text, '''        for (;;) {
            const m_byte status = memory().readByte(kYm2612A0);''', '''        pcHistogram(0x73298u, 26); pace(26);
        for (;;) {
            pcHistogram(0x73298u, 34); pace(34);
            const m_byte status = memory().readByte(kYm2612A0);''')
    if name == 'SoRControls.cpp':
        # sample_all_joypads ($810C) with its two sample_one_joypad calls costs
        # 552 cycles per VBlank (Genesis Plus GX profile of the Round 1 replay).
        return replace_once(text, '''    memory().writeWord(kZ80BusRequest, 0x0100u);
    cpu().a[1] = kIoPlayer1DataPort;''', '''    pcHistogram(0x810Cu, 552); pace(552);
    memory().writeWord(kZ80BusRequest, 0x0100u);
    cpu().a[1] = kIoPlayer1DataPort;''')
    if name == 'SoRManualFunctions.cpp':
        # game_infinite_loop ($3A2): moveq, move.w, add.w, move.l table, movea,
        # jsr (a0), jsr and bra.s per pass (88 cycles).
        return replace_once(text, '''        // moveq #0,d0 / move.w (game_state).w,d0 / add.w d0,d0
        const m_word state''', '''        // moveq #0,d0 / move.w (game_state).w,d0 / add.w d0,d0
        pcHistogram(0x3A2u, 88); pace(88);
        const m_word state''')
    if name == 'SoRInteractions.cpp':
        # player_normal_attack_input ($3028): 80 per call on average.
        text = replace_once(text, '''void StreetsOfRage::player_normal_attack_input(m_long entry_) {
    traceEnter(entry_);
''', '''void StreetsOfRage::player_normal_attack_input(m_long entry_) {
    traceEnter(entry_);
    pcHistogram(0x3028u, 80); pace(80);
''')
        # find_close_interaction_target ($3136): 58 when the player is already
        # lifting; otherwise 144 to set up the box, 44 per slot scanned (all 68
        # when nothing is found) and 24 to return.
        return replace_once(text, '''    const m_long target = findPickupTarget(memory(), player);
    if (target == 0u) {''', '''    const m_long target = findPickupTarget(memory(), player);
    {
        unsigned cost = 58;
        if ((memory().readByte(player + kObjState) & 0xFEu) != 0x28u)
            cost = 168 + 44 * (target ? (target - kObjectTable) / kObjectSlotSize + 1 : kInteractionScanSlots);
        pcHistogram(0x3136u, cost); pace(cost);
    }
    if (target == 0u) {''')
    if name == 'SoRMainMenus.cpp':
        # Update_PlayerObj's slot loop ($AE12): 40 cycles per empty slot and 154
        # per active slot around the handler calls (which charge themselves).
        # Outside the slot loop the routine costs 348 per update, plus 118 per
        # present player and 40 per absent one (Genesis Plus GX profile of the
        # Round 1 replay).
        text = replace_once(text, '''            const m_byte type = memory().readByte(cpu().a[0]);
            cpu().d[0]        = type;
            cpu().setFlag(CPU68K::FlagZ, type == 0);''', '''            const m_byte type = memory().readByte(cpu().a[0]);
            pcHistogram(0xAD8Eu, (player == 0 ? 348 : 0) + (type != 0 ? 118 : 40));
            pace((player == 0 ? 348 : 0) + (type != 0 ? 118 : 40));
            cpu().d[0]        = type;
            cpu().setFlag(CPU68K::FlagZ, type == 0);''')
        return replace_once(text, '''        const m_byte type = memory().readByte(cpu().a[0]);
        cpu().d[0]        = type;
        cpu().setNZClearVC(type, 0x80u);''', '''        const m_byte type = memory().readByte(cpu().a[0]);
        pcHistogram(0xAD8Eu, type != 0 ? 154 : 40); pace(type != 0 ? 154 : 40);
        cpu().d[0]        = type;
        cpu().setNZClearVC(type, 0x80u);''')
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
        # The incremental queue decodes on demand: parse the header and code
        # table here, then hand the table and bit position to a NemesisStream.
        ('template <typename ReadByte> NemesisResult decodeNemesis(ReadByte readByte, m_long source) {\n',
         STREAM + 'template <typename ReadByte> NemesisResult decodeNemesis(ReadByte readByte, m_long source, NemesisStream *stream = nullptr) {\n'),
        ('    MsbBitReader bits(reader, initialBits, 16);\n', """    if (stream != nullptr) {
        for (unsigned index = 0; index < 256; ++index)
            stream->table[index] = {table[index].length, table[index].token, table[index].valid};
        stream->position = initialPayloadCursor;
        stream->buffer   = initialBits;
        stream->bits     = 16;
        stream->xorMode  = xorMode;
        NemesisResult result;
        result.tableCycles          = %s;
        result.tileCount            = tileCount;
        result.initialBitBuffer     = initialBits;
        result.initialPayloadCursor = initialPayloadCursor;
        result.xorMode              = xorMode;
        return result;
    }
    MsbBitReader bits(reader, initialBits, 16);
""" % table_cycles_expression()),
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
        # Incremental Nemesis ($84BA/$8510): the original builds the code table
        # when a stream starts, then decodes up to five tiles per VBlank. The
        # host does the same (one burst of host work per queued art block was
        # a frame-time spike), and charges the table build at the start and
        # each call the tiles it decodes.
        ('''struct IncrementalNemesisState {
    std::vector<m_byte> decoded;
    std::size_t         uploadedTiles{};
    m_long              sourceEnd{};
    bool                xorMode{};
};''', '''struct IncrementalNemesisState {
    NemesisStream stream;   // SoR port: decoded tile by tile as uploaded
    std::size_t   uploadedTiles{};
    bool          xorMode{};
};'''),
        ('''    auto result = decodeNemesis(readByte, source);

    IncrementalNemesisState state;
    state.decoded            = std::move(result.data);
    state.sourceEnd          = result.sourceEnd;
    state.xorMode            = result.xorMode;''', '''    IncrementalNemesisState state;
    auto result = decodeNemesis(readByte, source, &state.stream);
    state.xorMode            = result.xorMode;'''),
        ('''    cpu().d[0] = 0;
    cpu().setNZClearVC(cpu().d[6], 0x80000000u);
    cpu().ssp += 4;''', '''    cpu().d[0] = 0;
    cpu().setNZClearVC(cpu().d[6], 0x80000000u);
    SOR_CHARGE_CPU(result.tableCycles);
    cpu().ssp += 4;'''),
        ('''    unsigned uploadedThisCall = 0;
''', '''    unsigned uploadedThisCall = 0;
    std::uint64_t sorTileCharge = 0;
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
        sorTileCharge += state.stream.tile(readByte, tile);
        for (std::size_t offset = 0; offset < 32u; offset += 2)
            vdp().writeDataPort(static_cast<m_word>((static_cast<m_word>(tile[offset]) << 8) | tile[offset + 1]));
'''),
        ('''    if (state.xorMode && state.uploadedTiles != 0) {
        const std::size_t lastRow = state.uploadedTiles * 32u - 4u;
        const m_long      row     = (static_cast<m_long>(state.decoded[lastRow]) << 24) |
                                    (static_cast<m_long>(state.decoded[lastRow + 1]) << 16) |
                                    (static_cast<m_long>(state.decoded[lastRow + 2]) << 8) | state.decoded[lastRow + 3];
        memory().writeLong(kIncrementalXorRow, row);
    }''', '''    if (uploadedThisCall != 0)
        sorTileCharge += %d;
    if (state.xorMode && state.uploadedTiles != 0)
        memory().writeLong(kIncrementalXorRow, state.stream.previousRow);'''  % INCREMENTAL_CALL),
        ('''        // The decoder's bit-level state is host-owned. Keep the RAM cursor
        // nonzero for queue producers and diagnostics while the stream is live.
        memory().writeLong(kArtQueue, state.sourceEnd);
        cpu().ssp += 4;
        return;''', '''        // The decoder's bit-level state is host-owned. Store the source
        // cursor, as the original stores A0, so the queue head stays nonzero.
        memory().writeLong(kArtQueue, state.stream.position);
        SOR_CHARGE_CPU(sorTileCharge);
        cpu().ssp += 4;
        return;'''),
        ('''    incrementalNemesis.erase(stateIt);
    cpu().ssp += 4;''', '''    incrementalNemesis.erase(stateIt);
    SOR_CHARGE_CPU(sorTileCharge);
    cpu().ssp += 4;'''),
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
