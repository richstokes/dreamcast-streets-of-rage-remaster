#include "pvr_tiles.hpp"
#include <cstdio>
#include <cstdint>
int main(){
 uint32_t seed=0x2612dc;
 for(unsigned n=0;n<8192;n++){
  uint8_t src[32];uint16_t packed[16];
  for(auto &v:src){seed=seed*1664525+1013904223;v=seed>>24;}
  sor::pack_pvr_tile4(src,packed);
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   unsigned morton=0;
   for(unsigned b=0;b<3;b++)morton|=((y>>b)&1)<<(b*2),morton|=((x>>b)&1)<<(b*2+1);
   unsigned got=(packed[morton/4]>>((morton%4)*4))&15;
   unsigned want=(src[y*4+x/2]>>(x&1?0:4))&15;
   if(got!=want){std::printf("tile %u pixel %u,%u: %u != %u\n",n,x,y,got,want);return 1;}
  }
 }
 std::puts("8192 tiles: 524288 palette indices preserve pixel positions and transparent index zero");
}
