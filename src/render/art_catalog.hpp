#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace sor {
// Replacement art for game objects, loaded from a package built by
// tools/make-placeholder-art.py (or a real art pipeline with the same format).
// A frame replaces one object animation frame, identified by the ROM address of
// its mapping record and its palette line; it is drawn unmirrored and flipped
// about its anchor for mirrored frames. Art is at twice the original's
// resolution: w, h and the anchor (the object's origin, at its feet) are in
// art pixels.
//
// Package, little-endian. "SORART01": ARGB1555 pages.
//   u8[8] magic, u32 pages, u32 frames
//   per page:  u16 width, u16 height, u16 pixels[width*height] (ARGB1555)
//   per frame: u32 mapping, u16 palette, u16 page, u16 u, v, w, h, s16 anchorX, anchorY
// "SORART02": one shared palette and 8-bit pages (half the size).
//   u8[8] magic, u32 pages, u32 frames, u32 colours (<= 256; index 0 transparent)
//   u16 palette[colours] (ARGB1555)
//   per page:  u16 width, u16 height, u8 indices[width*height]
//   per frame: as SORART01
// "SORART03": as SORART02 with each page's indices zlib-compressed.
//   per page:  u16 width, u16 height, u32 packed size, u8 zlib[packed size]
struct ArtFrame {
    uint32_t mapping;
    uint16_t palette,page,u,v,w,h;
    int16_t anchorX,anchorY;
};
struct ArtPage {
    uint16_t width,height;
    const uint16_t *pixels;    // SORART01, else null
    const uint8_t *indices;    // SORART02, or SORART03 once inflated; else null
    const uint8_t *packed;     // SORART03: zlib stream, else null
    uint32_t packedSize;
};
class ArtCatalog {
public:
    // Parses a package held in memory (not copied; it must outlive the catalog).
    // SORART03 pages are inflated into memory the catalog owns, unless
    // inflatePages is false: then each page is fetched with inflate() (the
    // Dreamcast uploads one page at a time and keeps none in main RAM).
    bool load(const uint8_t *data,size_t size,bool inflatePages=true);
    // Inflates a SORART03 page into out (width*height bytes).
    bool inflate(const ArtPage &page,uint8_t *out) const;
    const ArtFrame *find(uint32_t mapping,int palette) const;
    const std::vector<ArtFrame> &frames() const {return frames_;}
    const std::vector<ArtPage> &pages() const {return pages_;}
    // SORART02's palette (empty for SORART01); entry 0 is transparent.
    const std::vector<uint16_t> &palette() const {return palette_;}
    uint16_t texel(const ArtPage &page,size_t index) const
        {return page.indices?palette_[page.indices[index]]:page.pixels[index];}
    bool empty() const {return frames_.empty();}
private:
    std::vector<ArtFrame> frames_;   // sorted by (mapping, palette)
    std::vector<ArtPage> pages_;
    std::vector<uint16_t> palette_;
    std::vector<std::vector<uint8_t>> inflated_;
};
}
