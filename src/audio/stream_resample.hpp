#pragma once
#include <cstdint>

namespace sor {
// Copy n stereo frames, advancing take frames through a power-of-two ring.
// The source of frame i is read + floor(i * take / n), as in the original
// stream callback. Carrying the remainder keeps that exact nearest-neighbour
// mapping without a software division for every frame on SH-4.
template<unsigned Capacity>
inline void resample_stereo_ring(const int16_t *ring,uint32_t read,unsigned n,unsigned take,int16_t *output){
    static_assert(Capacity && !(Capacity&(Capacity-1)));
    if(!n)return;
    const unsigned step=take/n,remainder=take-step*n;
    unsigned fraction=0;
    for(unsigned i=0;i<n;i++){
        const unsigned at=read&(Capacity-1);
        output[i*2]=ring[at*2];output[i*2+1]=ring[at*2+1];
        read+=step;
        fraction+=remainder;
        if(fraction>=n){fraction-=n;read++;}
    }
}
}
