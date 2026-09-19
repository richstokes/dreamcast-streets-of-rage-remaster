#include "diagnostics.hpp"
#include "replay.hpp"
#include <cstdio>
#include <cstdint>
#include <stdexcept>
namespace {
struct Segment{uint32_t frames;uint16_t p1,p2,address;uint8_t mask,value;uint32_t flags;};
constexpr unsigned maxSegments=4096; // 80 KiB
Segment segments[maxSegments];unsigned count=0,index=0;uint32_t remaining=0,played=0;
uint32_t le(const uint8_t *b,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;i++)v|=uint32_t(b[i])<<(i*8);return v;}
PlayerControlsState decode(uint16_t b){
 PlayerControlsState p{};p.connected=true;p.up=b&1;p.down=b&2;p.left=b&4;p.right=b&8;
 p.b=b&16;p.c=b&32;p.a=b&64;p.start=b&128;return p;
}
}
bool replay_load(const char *path){
 count=index=remaining=played=0;
 auto f=fopen(path,"rb");if(!f)return false;
 uint8_t h[8];if(fread(h,1,8,f)!=8||h[0]!='S'||h[1]!='R'||h[2]!='P'||(h[3]!='1'&&h[3]!='2')){fclose(f);return false;}
 unsigned n=le(h+4,4);if(!n||n>maxSegments){fclose(f);return false;}
 for(unsigned i=0;i<n;i++){
  const unsigned size=h[3]=='2'?16:8;
  uint8_t b[16]{};if(fread(b,1,size,f)!=size){fclose(f);return false;}
  segments[i]={le(b,4),uint16_t(le(b+4,2)),uint16_t(le(b+6,2)),uint16_t(le(b+8,2)),b[10],b[11],le(b+12,4)};
  if(!segments[i].frames||segments[i].frames>60000||segments[i].p1>255||segments[i].p2>255||segments[i].flags>1||(segments[i].flags && (segments[i].p1 || segments[i].p2 || (segments[i].value&segments[i].mask)!=segments[i].value))){fclose(f);return false;}
 }
 if(fgetc(f)==EOF){count=n;remaining=segments[0].frames;sor_log("REPLAY enabled: %u segments (SRP%c)\n",n,h[3]);}fclose(f);
 return count!=0;
}
bool replay_finished(){return count && index==count;}
bool replay_poll(PlayersControlState &p,const uint8_t *ram){
 if(!count)return false;
 while(index<count && segments[index].flags &&
       (ram[segments[index].address]&segments[index].mask)==segments[index].value){
  sor_log("REPLAY gate %u matched at frame %lu after %lu idle frames\n",index,(unsigned long)played,(unsigned long)(segments[index].frames-remaining));
  if(++index<count)remaining=segments[index].frames;
 }
 if(index==count){p={decode(0),decode(0)};return true;}
 played++;
 auto &s=segments[index];p={decode(s.p1),decode(s.p2)};
 if(s.flags){
  if(!remaining){sor_log("REPLAY gate %u timed out after %lu idle frames\n",index,(unsigned long)s.frames);throw std::runtime_error("Replay state gate timed out");}
  --remaining;
 }else if(!--remaining){
  sor_log("REPLAY segment %u complete\n",index);
  if(++index<count)remaining=segments[index].frames;
 }
 return true;
}
