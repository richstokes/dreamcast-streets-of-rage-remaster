"""The replacement-art colour key (src/render/art_catalog.cpp, colour_key)."""


def colour_key(line, mask=0xFFFE):
    """line: the 16 CRAM words of a palette line; bit i of mask: entry i counts."""
    h = 2166136261
    for i in range(1, 16):
        if not mask >> i & 1:
            continue
        v = line[i] & 0x0EEE
        h = ((h ^ (v & 255)) * 16777619) & 0xFFFFFFFF
        h = ((h ^ (v >> 8)) * 16777619) & 0xFFFFFFFF
    return (h >> 16 ^ h) & 0xFFFF
