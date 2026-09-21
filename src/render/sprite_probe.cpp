#include "sprite_probe.hpp"
#include "VDPState.hpp"
#include <cstring>
namespace sor {
namespace {
constexpr uint32_t ramSat=0xFFDA00;   // sprite_attribute_table_buffer
}
SpriteProbe &sprite_probe(){static SpriteProbe probe;return probe;}
void SpriteProbe::beginBuild(){
    current_^=1;
    auto &b=builds_[current_];
    b.count=0;b.emitted=0;b.serial=++serial_;
    open_=false;
}
void SpriteProbe::beginObject(uint16_t slot,uint8_t type,uint32_t mapping,bool flip,int16_t x,int16_t y,uint16_t tileBase,uint32_t sat,uint32_t set,int16_t level,bool screen){
    pending_=ProbedObject{mapping&0xFFFFFF,set&0xFFFFFF,slot,tileBase,x,y,type,0,0,flip,level,screen};
    start_=sat&0xFFFFFF;open_=true;
}
void SpriteProbe::endObject(uint32_t sat,const uint8_t *ram){
    if(!open_)return;
    open_=false;
    auto &b=builds_[current_];
    sat&=0xFFFFFF;
    if(start_<ramSat||sat<start_||sat>ramSat+SpriteBuild::MAX_RECORDS*8)return;
    const unsigned first=(start_-ramSat)/8,last=(sat-ramSat)/8;
    // Everything emitted so far, the HUD's pieces before the first object included.
    std::memcpy(b.records,ram+(ramSat&0xFFFF),last*8);b.emitted=uint8_t(last);
    if(last==first||b.count==SpriteBuild::MAX_OBJECTS)return;
    pending_.first=uint8_t(first);pending_.count=uint8_t(last-first);
    b.objects[b.count++]=pending_;
}
const SpriteBuild *SpriteProbe::displayed(const VDPState &s) const{
    const int base=s.satBase();
    for(int i=0;i<2;i++){
        const auto &b=builds_[current_^i];
        if(!b.serial||!b.count)continue;
        bool same=true;
        for(unsigned r=0;r<b.emitted&&same;r++)
            same=std::memcmp(s.vram_+((base+r*8)&0xFFFF),b.records+r*8,8)==0;
        if(same)return &b;
    }
    return nullptr;
}
}
