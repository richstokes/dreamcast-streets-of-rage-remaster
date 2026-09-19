#include "art_catalog.hpp"
#include <algorithm>
#include <cstring>
namespace sor {
namespace {
struct Reader {
    const uint8_t *p,*end;
    bool has(size_t n) const {return size_t(end-p)>=n;}
    uint16_t u16(){uint16_t v=uint16_t(p[0]|p[1]<<8);p+=2;return v;}
    uint32_t u32(){uint32_t v=uint32_t(p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24);p+=4;return v;}
};
}
bool ArtCatalog::load(const uint8_t *data,size_t size){
    frames_.clear();pages_.clear();
    Reader r{data,data+size};
    if(!r.has(16)||std::memcmp(data,"SORART01",8))return false;
    r.p+=8;
    const uint32_t pages=r.u32(),frames=r.u32();
    for(uint32_t i=0;i<pages;i++){
        if(!r.has(4))return false;
        ArtPage page{r.u16(),r.u16(),nullptr};
        const size_t bytes=size_t(page.width)*page.height*2;
        // Pixel data must be 2-byte aligned to be read in place.
        if(!r.has(bytes)||(uintptr_t(r.p)&1))return false;
        page.pixels=reinterpret_cast<const uint16_t*>(r.p);r.p+=bytes;
        pages_.push_back(page);
    }
    for(uint32_t i=0;i<frames;i++){
        if(!r.has(20))return false;
        ArtFrame f;
        f.mapping=r.u32();f.palette=r.u16();f.page=r.u16();f.u=r.u16();f.v=r.u16();f.w=r.u16();f.h=r.u16();
        f.anchorX=int16_t(r.u16());f.anchorY=int16_t(r.u16());
        if(f.page>=pages_.size()||f.u+f.w>pages_[f.page].width||f.v+f.h>pages_[f.page].height)return false;
        frames_.push_back(f);
    }
    std::sort(frames_.begin(),frames_.end(),[](const ArtFrame &a,const ArtFrame &b){
        return a.mapping!=b.mapping?a.mapping<b.mapping:a.palette<b.palette;});
    return true;
}
const ArtFrame *ArtCatalog::find(uint32_t mapping,int palette) const{
    auto it=std::lower_bound(frames_.begin(),frames_.end(),std::pair<uint32_t,int>(mapping,palette),
        [](const ArtFrame &f,const std::pair<uint32_t,int> &k){return f.mapping!=k.first?f.mapping<k.first:f.palette<k.second;});
    return it!=frames_.end()&&it->mapping==mapping&&it->palette==palette?&*it:nullptr;
}
}
