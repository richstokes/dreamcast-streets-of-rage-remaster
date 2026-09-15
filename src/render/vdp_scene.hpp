#pragma once
#include "VDPState.hpp"
#include "VDPRenderer.hpp"
#include <cstdint>
#include <cstddef>
namespace sor {
struct TileQuad {
    uint16_t tile;
    uint16_t x,y,w,h;
    uint8_t palette,depth,u0,v0,u1,v1;
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
private:
    VDPState previous;
    bool cacheValid=false;
    uint16_t spriteFlags=0;
    bool buildImpl(VDPState &,bool keepPlanes);
    void spriteLayers(VDPState &);
    void plane(const VDPState &,int plane);
    void window(const VDPState &);
    void add(uint16_t entry,int x,int y,int w,int h,int px,int py,int lowDepth);
};
// CPU oracle for the GPU command stream. Used only by host tests.
void raster_scene(const VdpScene &,const VDPState &,uint16_t *out);
}
