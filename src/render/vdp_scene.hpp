#pragma once
#include "VDPState.hpp"
#include "VDPRenderer.hpp"
#include <cstdint>
#include <cstddef>
namespace sor {
class ArtCatalog;
struct TileQuad {
    uint16_t tile;
    uint16_t x,y,w,h;
    uint8_t palette,depth,u0,v0,u1,v1;
};
// Enhanced rendering: a hardware sprite piece's 8x8 cell, drawn as its own
// quad, and an object drawn with replacement art. `order` is the sprite's
// position in the SAT's link order (0 is in front); `layer` its priority.
struct SpriteTile {
    uint16_t tile;
    int16_t x,y;
    uint8_t palette,layer,order;
    bool hflip,vflip;
};
struct ArtDraw {
    uint32_t frame;          // index into ArtCatalog::frames()
    int16_t x,y;             // the object's anchor on screen
    uint8_t layer,order;
    bool flip;
};
class VdpScene {
public:
    static constexpr size_t MAX_QUADS=32768;
    TileQuad quads[MAX_QUADS];
    size_t count=0;
    alignas(32) uint16_t sprites[2][512*256]{};
    uint16_t colors[64]{},background=0;
    int width=320,height=224;
    bool build(VDPState &,VDPRenderer &);
    bool buildCached(VDPState &,VDPRenderer &);
    bool reused=false,planesReused=false;
    int spriteTop[2]{256,256},spriteBottom[2]{};
    static uint16_t rgb1555(unsigned r,unsigned g,unsigned b);
    // Enhanced mode: sprites become spriteTiles and artDraws instead of the
    // two composited layers (which are still built: they also produce the
    // VDP's sprite overflow and collision status bits the game can read).
    // Objects with no art in `art` keep their original pieces. VDP sprite
    // limits per line do not apply to the enhanced drawing.
    bool enhanced=false;
    const ArtCatalog *art=nullptr;
    static constexpr size_t MAX_SPRITE_TILES=80*16;
    SpriteTile spriteTiles[MAX_SPRITE_TILES];
    size_t spriteTileCount=0;
    ArtDraw artDraws[80];
    size_t artCount=0;
private:
    VDPState previous;
    bool cacheValid=false;
    uint16_t spriteFlags=0;
    bool builtEnhanced_=false;
    bool buildImpl(VDPState &,bool keepPlanes);
    void spriteLayers(VDPState &);
    void enhancedSprites(const VDPState &);
    void plane(const VDPState &,int plane);
    void window(const VDPState &);
    void add(uint16_t entry,int x,int y,int w,int h,int px,int py,int lowDepth);
};
// CPU oracle for the GPU command stream. Used only by host tests.
void raster_scene(const VdpScene &,const VDPState &,uint16_t *out);
// Host preview of an enhanced scene at twice the resolution (640x448 for H40).
void raster_enhanced(const VdpScene &,const VDPState &,uint16_t *out,int pitch);
}
