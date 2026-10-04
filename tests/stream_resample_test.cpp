#include "stream_resample.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

static uint64_t checked=0;

template<unsigned Capacity>
static void check(uint32_t read,unsigned n,unsigned take){
    // Different left/right and adjacent values expose wrong indices, stereo
    // swaps, duplicated writes and wrap errors. Do not assume smooth audio.
    std::array<int16_t,Capacity*2> ring;
    for(unsigned i=0;i<ring.size();i++)ring[i]=int16_t((i*40503u+19777u)&65535);
    const auto original=ring;
    constexpr int16_t guard=12345;
    std::vector<int16_t> output(n*2+2,guard);
    sor::resample_stereo_ring<Capacity>(ring.data(),read,n,take,output.data()+1);
    assert(output.front()==guard && output.back()==guard);
    assert(ring==original);
    for(unsigned i=0;i<n;i++){
        // Independent closed-form oracle, including the callback's uint32
        // counter wrap before ring addressing. Wide multiply avoids overflow.
        const unsigned at=uint32_t(read+uint64_t(i)*take/n)%Capacity;
        assert(output[i*2+1]==ring[at*2]);
        assert(output[i*2+2]==ring[at*2+1]);
        checked++;
    }
}

int main(){
    // Exhaust small ratios and every ring offset, including take=0, n=0,
    // repeated frames and ratios above two input frames per output frame.
    for(unsigned n=0;n<=32;n++)for(unsigned take=0;take<=36;take++)
        for(unsigned read=0;read<16;read++)check<16>(read,n,take);
    constexpr unsigned capacity=16384;
    const uint32_t starts[]={0,1,511,512,1023,1024,2047,2048,capacity-2048,
        capacity-1024,capacity-512,capacity-2,capacity-1,capacity,capacity+1,
        UINT32_MAX-2048,UINT32_MAX-1024,UINT32_MAX-512,UINT32_MAX-1,UINT32_MAX};
    uint32_t random=0x5121024;
    for(unsigned n:{512u,1024u,2048u})for(int adjust=-4;adjust<=4;adjust++){
        const unsigned take=unsigned(int(n)+adjust);
        for(uint32_t read:starts)check<capacity>(read,n,take);
        for(unsigned i=0;i<128;i++){
            random=random*1664525u+1013904223u;
            check<capacity>(random,n,take);
        }
    }
    std::printf("audio stream: %llu stereo frames match division oracle; ring/output bounds and wraps pass\n",
        (unsigned long long)checked);
}
