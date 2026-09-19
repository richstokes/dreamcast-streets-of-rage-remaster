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
// Package ("SORART01", little-endian):
//   u8[8] magic, u32 pages, u32 frames
//   per page:  u16 width, u16 height, u16 pixels[width*height] (ARGB1555)
//   per frame: u32 mapping, u16 palette, u16 page, u16 u, v, w, h, s16 anchorX, anchorY
struct ArtFrame {
    uint32_t mapping;
    uint16_t palette,page,u,v,w,h;
    int16_t anchorX,anchorY;
};
struct ArtPage {
    uint16_t width,height;
    const uint16_t *pixels;
};
class ArtCatalog {
public:
    // Parses a package held in memory (not copied; it must outlive the catalog).
    bool load(const uint8_t *data,size_t size);
    const ArtFrame *find(uint32_t mapping,int palette) const;
    const std::vector<ArtFrame> &frames() const {return frames_;}
    const std::vector<ArtPage> &pages() const {return pages_;}
    bool empty() const {return frames_.empty();}
private:
    std::vector<ArtFrame> frames_;   // sorted by (mapping, palette)
    std::vector<ArtPage> pages_;
};
}
