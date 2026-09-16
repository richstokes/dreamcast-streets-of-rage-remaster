#pragma once
#include <cstdint>
namespace sor {
// Genesis stores left pixels in the high nibble. PowerVR's Morton order
// places each 2x2 group in a little-endian word: TL, BL, TR, BR nibbles.
inline void pack_pvr_tile4(const uint8_t *source,uint16_t *target) {
    constexpr unsigned spread[]={0,1,4,5};
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++) {
        unsigned top=source[y*8+x],bottom=source[y*8+x+4];
        target[spread[y]+2*spread[x]]=(top>>4)|((bottom&0xf0))|
            ((top&15)<<8)|((bottom&15)<<12);
    }
}
}
