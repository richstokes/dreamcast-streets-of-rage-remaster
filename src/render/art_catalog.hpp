#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace sor {
// Replacement art for game objects, loaded from a package built by
// tools/make-enhanced-art.py. docs/REMASTER.md explains the whole path.
//
// A frame replaces one object animation frame. Its key is the ROM address of
// the frame's mapping record and the colours it was made for: colour_key() of
// the sprite's CRAM line, over the entries the object's art uses (mask). The
// palette line alone is not enough: enemy families share frames and a line
// while the game loads different colours into it (green, purple and yellow Signals), and
// replacement art has its colours baked in. The whole line is too much: lines
// also hold colours the object never uses, which stages cycle. A line whose
// used colours match no art (hit flashes, fades) finds nothing, and the object
// is drawn from its original pieces with the game's colours.
//
// A frame is drawn unmirrored, and flipped about its anchor for mirrored
// frames. Art is at twice the original's resolution: w, h and the anchor (the
// object's origin, at its feet) are in art pixels.
//
// All the game's art does not fit in PowerVR memory at once. Each page lists
// the rounds (bit 0 = round 1) whose objects it holds and, for player art, the
// character (1 Adam, 2 Axel, 3 Blaze; 0: not a player's); select() chooses the
// pages of the current round and of the characters in play. Pages are stored
// most important first: a loader that runs out of memory marks the rest
// unloaded, and their frames fall back to the original pieces.
//
// In-between poses (smooth animation): a frame with `from` set is shown for a
// few ticks when an object goes from mapping `from` to `mapping`. They are on
// pages of their own (character bit 7), stored last, and selected only while
// smooth animation is on: they take what memory the ordinary art leaves.
//
// Fades and flashes: the game fades by subtracting a step from each colour
// channel of a line (clamped at black) and flashes by adding one (clamped at
// white). A line that is a frame's colours (`line`) changed that way finds the
// frame with a tint: a scale and an offset per channel, which the renderers
// apply to the art (PowerVR: vertex colour and offset colour). Anything else
// (another look whose art is not loaded, a palette swap) finds nothing, and
// the original pieces are drawn in the game's colours.
//
// Package "SORART06", little-endian ("SORART05": no `line`; "SORART04": no `from`):
//   u8[8] magic, u32 pages, u32 frames, u32 palettes (1-3)
//   u16 palette[palettes][256] (ARGB1555; index 0 transparent)
//   per page:  u16 width, u16 height, u16 rounds, u8 palette, u8 character, u32 packed size,
//              u8 zlib[packed size] -> u8 indices[width*height]
//   per frame: u32 mapping, u16 colour key, u16 mask (bit i: CRAM entry i counts),
//              u16 page, u16 u, v, w, h, s16 anchorX, anchorY, u32 from,
//              u16 line[16] (the CRAM line the art's colours were made from)
struct ArtFrame {
    uint32_t mapping,from;
    uint16_t colours,mask,page,u,v,w,h;
    int16_t anchorX,anchorY;
    uint16_t line[16];       // all zero in older packages: no tinting
};
// How art is recoloured to follow a fade or flash: out = art*scale/255 + offset, per channel (r, g, b).
struct ArtTint {
    uint8_t scale[3]{255,255,255},offset[3]{0,0,0};
    bool identity() const {return scale[0]==255&&scale[1]==255&&scale[2]==255&&!offset[0]&&!offset[1]&&!offset[2];}
};
struct ArtPage {
    uint16_t width,height,rounds;
    uint8_t palette,character;   // character bit 7: in-between poses
    bool inbetween() const {return character&0x80;}
    const uint8_t *indices;    // inflated by the catalog, else null
    const uint8_t *packed;     // zlib stream
    uint32_t packedSize;
};
// Key of the masked entries of a CRAM line (cram + line*16, 9-bit colour).
uint16_t colour_key(const uint16_t *line,uint16_t mask);
class ArtCatalog {
public:
    // Parses a package held in memory (not copied; it must outlive the catalog).
    // Pages are inflated into memory the catalog owns, unless inflatePages is
    // false: then each page is fetched with inflate() (the Dreamcast uploads
    // one page at a time and keeps none in main RAM).
    bool load(const uint8_t *data,size_t size,bool inflatePages=true);
    // Inflates a page into out (width*height bytes).
    bool inflate(const ArtPage &page,uint8_t *out) const;
    // Frames on other pages are not found. round 0: every round; characters:
    // bit c set when character c is in play (ignored with round 0).
    void select(unsigned round,unsigned characters,bool inbetweens=true);
    // A selected page the loader could not fit.
    void setUnloaded(size_t page){wanted_[page]=0;}
    bool wanted(size_t page) const {return wanted_[page];}
    // line: the 16 CRAM words of the sprite's palette line.
    // tint: set when given; without it only exact colours match.
    const ArtFrame *find(uint32_t mapping,const uint16_t *line,ArtTint *tint=nullptr) const {return find(0,mapping,line,tint);}
    // The in-between pose from one mapping to another, if there is one.
    const ArtFrame *between(uint32_t from,uint32_t mapping,const uint16_t *line,ArtTint *tint=nullptr) const {return from?find(from,mapping,line,tint):nullptr;}
    bool hasInbetweens() const {return !frames_.empty()&&frames_.back().from;}
    const std::vector<ArtFrame> &frames() const {return frames_;}
    const std::vector<ArtPage> &pages() const {return pages_;}
    // 256 entries per palette; entry 0 of each is transparent. The Dreamcast
    // puts palette n in PowerVR palette bank n+1 (bank 0 holds the tile palettes).
    const std::vector<uint16_t> &palettes() const {return palette_;}
    uint16_t texel(const ArtPage &page,size_t index) const {return palette_[page.palette*256+page.indices[index]];}
    bool empty() const {return frames_.empty();}
private:
    const ArtFrame *find(uint32_t from,uint32_t mapping,const uint16_t *line,ArtTint *tint) const;
    std::vector<ArtFrame> frames_;   // sorted by (from, mapping): ordinary frames first
    std::vector<ArtPage> pages_;
    std::vector<uint16_t> palette_;
    std::vector<std::vector<uint8_t>> inflated_;
    std::vector<uint8_t> wanted_;
};
}
