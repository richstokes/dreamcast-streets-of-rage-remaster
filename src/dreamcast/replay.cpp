#include "replay.hpp"
#include <cstdio>
#include <cstdint>
namespace {
struct Segment{uint32_t frames;uint16_t p1,p2;};
Segment segments[128];unsigned count=0,index=0;uint32_t remaining=0;
uint32_t le(const uint8_t *b,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;i++)v|=uint32_t(b[i])<<(i*8);return v;}
PlayerControlsState decode(uint16_t b){
 PlayerControlsState p{};p.connected=true;p.up=b&1;p.down=b&2;p.left=b&4;p.right=b&8;
 p.b=b&16;p.c=b&32;p.a=b&64;p.start=b&128;return p;
}
}
bool replay_load(const char *path){
 count=index=remaining=0;
 auto f=fopen(path,"rb");if(!f)return false;
 uint8_t h[8];if(fread(h,1,8,f)!=8||h[0]!='S'||h[1]!='R'||h[2]!='P'||h[3]!='1'){fclose(f);return false;}
 unsigned n=le(h+4,4);if(!n||n>128){fclose(f);return false;}
 for(unsigned i=0;i<n;i++){
  uint8_t b[8];if(fread(b,1,8,f)!=8){fclose(f);return false;}
  segments[i]={le(b,4),uint16_t(le(b+4,2)),uint16_t(le(b+6,2))};
  if(!segments[i].frames||segments[i].frames>60000||segments[i].p1>255||segments[i].p2>255){fclose(f);return false;}
 }
 if(fgetc(f)==EOF){count=n;remaining=segments[0].frames;printf("REPLAY enabled: %u frame-counted segments\n",n);}fclose(f);
 return count!=0;
}
uint32_t replay_total_frames(){uint32_t total=0;for(unsigned i=0;i<count;i++)total+=segments[i].frames;return total;}
bool replay_poll(PlayersControlState &p){
 if(!count)return false;
 if(index==count){p={decode(0),decode(0)};return true;}
 auto &s=segments[index];p={decode(s.p1),decode(s.p2)};
 if(!--remaining){printf("REPLAY segment %u complete\n",index);if(++index<count)remaining=segments[index].frames;}
 return true;
}
