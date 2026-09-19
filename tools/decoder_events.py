"""Count decode events for SoR's Nemesis, Kosinski and Enigma data.

Mirrors the hand-written decoders in SoRDecompress.cpp and counts the events
whose cost dominates the original MC68000 routines. tools/fit-decoder-cycles.py
fits per-event cycle costs to Musashi measurements of the ROM's own routines.
"""


class Bits:
    def __init__(self, data, pos, buffer=0, count=0):
        self.data, self.pos, self.buffer, self.count, self.fetched = data, pos, buffer, count, 0

    def fill(self, n):
        while self.count < n:
            self.buffer = (self.buffer << 8) | self.data[self.pos]; self.pos += 1; self.count += 8; self.fetched += 1

    def peek(self, n):
        self.fill(n); return (self.buffer >> (self.count - n)) & ((1 << n) - 1)

    def read(self, n):
        if n == 0: return 0
        v = self.peek(n); self.count -= n; self.buffer &= (1 << self.count) - 1; return v


def nemesis(rom, src):
    e = dict(definitions=0, table_entries=0, groups=0, codes=0, inline=0, nibbles=0, rows=0, xor_rows=0, fetched=0)
    header = (rom[src] << 8) | rom[src + 1]; pos = src + 2
    xor, tiles = bool(header & 0x8000), header & 0x7fff
    group = rom[pos]; pos += 1
    table = {}
    while group != 0xff:
        e['groups'] += 1
        pixel = group & 15
        while True:
            d = rom[pos]; pos += 1
            if d >= 0x80: group = d; break
            length = d & 15; token = (d & 0x70) | pixel; code = rom[pos]; pos += 1
            if not 1 <= length <= 8 or code >= (1 << length):
                raise ValueError('invalid Nemesis code')
            e['definitions'] += 1
            first = code << (8 - length)
            for i in range(first, first + (1 << (8 - length))):
                if i in table: raise ValueError('overlapping Nemesis code')
                table[i] = (length, token); e['table_entries'] += 1
    initial = (rom[pos] << 8) | rom[pos + 1]; pos += 2
    bits = Bits(rom, pos, initial, 16)
    rows = tiles * 8; nibbles = 0
    while rows:
        prefix = bits.peek(8)
        if prefix >= 0xfc:
            bits.read(6); token = bits.read(7); e['inline'] += 1
        else:
            if prefix not in table: raise ValueError('undefined Nemesis code')
            length, token = table[prefix]; bits.read(length); e['codes'] += 1
        for _ in range((token >> 4) + 1):
            e['nibbles'] += 1; nibbles += 1
            if nibbles == 8:
                nibbles = 0; rows -= 1; e['rows'] += 1; e['xor_rows'] += xor
                if rows == 0: break
    e['fetched'] = bits.fetched
    return e, tiles * 32


def kosinski(rom, src):
    e = dict(descriptor_bits=0, descriptor_fetches=1, literals=0, short=0, short_bytes=0, long=0, long_bytes=0, extended=0, extended_bytes=0)
    pos = src
    desc = rom[pos] | (rom[pos + 1] << 8); pos += 2; remaining = 16
    out = 0

    def bit():
        nonlocal desc, remaining, pos
        e['descriptor_bits'] += 1
        b = desc & 1; desc >>= 1; remaining -= 1
        if remaining == 0:
            desc = rom[pos] | (rom[pos + 1] << 8); pos += 2; remaining = 16; e['descriptor_fetches'] += 1
        return b
    while True:
        if out > 0x4000: raise ValueError('Kosinski output too long')
        if bit():
            pos += 1; out += 1; e['literals'] += 1; continue
        if bit() == 0:
            length = ((bit() << 1) | bit()) + 2
            if 0x100 - rom[pos] > out: raise ValueError('Kosinski reference before output')
            pos += 1
            e['short'] += 1; e['short_bytes'] += length; out += length; continue
        low, high = rom[pos], rom[pos + 1]; pos += 2
        code = high & 7
        if 0x10000 - (0xe000 | ((high & 0xf8) << 5) | low) > out: raise ValueError('Kosinski reference before output')
        if code:
            e['long'] += 1; e['long_bytes'] += code + 2; out += code + 2; continue
        ext = rom[pos]; pos += 1
        if ext == 0: break
        if ext != 1:
            e['extended'] += 1; e['extended_bytes'] += ext + 1; out += ext + 1
    return e, out


def enigma(rom, src, base, header=False):
    e = dict(ops=0, inline_words=0, inline_bits=0, words=0, header=int(header),
             words_increment=0, words_common=0, words_repeat=0, words_up=0, words_down=0, words_inline=0, fetched=0)
    if header: src += 8
    index_bits, mask = rom[src], rom[src + 1]
    if index_bits > 16: raise ValueError('invalid Enigma tile-index width')
    bits = Bits(rom, src + 6)
    out = 0

    def inline():
        e['inline_words'] += 1
        for m in (0x10, 8, 4, 2, 1):
            if mask & m:
                bits.read(1); e['inline_bits'] += 1
        if index_bits:
            bits.read(index_bits); e['inline_bits'] += index_bits
    while True:
        if out > 0x2000: raise ValueError('Enigma output too long')
        op = bits.read(1)
        op = bits.read(1) if op == 0 else 4 + bits.read(2)
        count = bits.read(4); e['ops'] += 1
        if op == 7 and count == 15: break
        run = count + 1; e['words'] += run; out += 2 * run
        e[('words_increment', 'words_common', None, None, 'words_repeat', 'words_up', 'words_down', 'words_inline')[op]] += run
        if op in (4, 5, 6): inline()
        elif op == 7:
            for _ in range(run): inline()
    e['fetched'] = bits.fetched
    return e, out


def events(rom, entry, src, d0):
    if entry in (0x8192, 0x81a4): return nemesis(rom, src)
    if entry == 0x85a2: return kosinski(rom, src)
    if entry == 0x82d2: return enigma(rom, src, d0, header=True)
    if entry == 0x12832: return enigma(rom, 0x12842, 0)
    return enigma(rom, src, d0)
