#include "art_catalog.hpp"
#include <algorithm>
#include <cstring>
#include <zlib.h>
namespace sor {
namespace {
struct Reader {
    const uint8_t *p,*end;
    bool has(size_t n) const {return size_t(end-p)>=n;}
    uint16_t u16(){uint16_t v=uint16_t(p[0]|p[1]<<8);p+=2;return v;}
    uint32_t u32(){uint32_t v=uint32_t(p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24);p+=4;return v;}
};
}
uint16_t colour_key(const uint16_t *line,uint16_t mask){
    // FNV-1a over the masked entries (tools/art_key.py computes the same value).
    uint32_t h=2166136261u;
    for(int i=1;i<16;i++){
        if(!(mask>>i&1))continue;
        const unsigned v=line[i]&0x0EEE;
        h=(h^(v&255))*16777619u;h=(h^(v>>8))*16777619u;
    }
    return uint16_t(h>>16^h);
}
void ArtCatalog::select(unsigned round,unsigned characters){
    for(size_t i=0;i<pages_.size();i++){
        const auto &p=pages_[i];
        wanted_[i]=!round||((p.rounds>>(round-1)&1)&&(!p.character||(characters>>p.character&1)));
    }
}
bool ArtCatalog::inflate(const ArtPage &page,uint8_t *out) const{
    uLongf n=uLongf(page.width)*page.height;
    return page.packed&&uncompress(out,&n,page.packed,page.packedSize)==Z_OK&&n==uLongf(page.width)*page.height;
}
bool ArtCatalog::load(const uint8_t *data,size_t size,bool inflatePages){
    frames_.clear();pages_.clear();palette_.clear();inflated_.clear();
    Reader r{data,data+size};
    if(!r.has(20)||std::memcmp(data,"SORART04",8))return false;
    r.p+=8;
    const uint32_t pages=r.u32(),frames=r.u32(),palettes=r.u32();
    if(!palettes||palettes>3||!r.has(palettes*512))return false;
    palette_.resize(palettes*256);
    for(auto &c:palette_)c=r.u16();
    for(uint32_t i=0;i<palettes;i++)palette_[i*256]=0;
    inflated_.reserve(pages);
    for(uint32_t i=0;i<pages;i++){
        if(!r.has(12))return false;
        ArtPage page{r.u16(),r.u16(),r.u16(),0,0,nullptr,nullptr,0};
        page.palette=*r.p++;page.character=*r.p++;page.packedSize=r.u32();
        if(page.palette>=palettes)return false;
        if(!r.has(page.packedSize))return false;
        page.packed=r.p;r.p+=page.packedSize;
        if(inflatePages){
            inflated_.emplace_back(size_t(page.width)*page.height);
            if(!inflate(page,inflated_.back().data()))return false;
            page.indices=inflated_.back().data();   // the inner buffer does not move
        }
        pages_.push_back(page);
    }
    for(uint32_t i=0;i<frames;i++){
        if(!r.has(22))return false;
        ArtFrame f;
        f.mapping=r.u32();f.colours=r.u16();f.mask=r.u16();f.page=r.u16();f.u=r.u16();f.v=r.u16();f.w=r.u16();f.h=r.u16();
        f.anchorX=int16_t(r.u16());f.anchorY=int16_t(r.u16());
        if(f.page>=pages_.size()||f.u+f.w>pages_[f.page].width||f.v+f.h>pages_[f.page].height)return false;
        frames_.push_back(f);
    }
    wanted_.assign(pages_.size(),1);
    std::stable_sort(frames_.begin(),frames_.end(),[](const ArtFrame &a,const ArtFrame &b){return a.mapping<b.mapping;});
    return true;
}
const ArtFrame *ArtCatalog::find(uint32_t mapping,const uint16_t *line) const{
    auto it=std::lower_bound(frames_.begin(),frames_.end(),mapping,[](const ArtFrame &f,uint32_t m){return f.mapping<m;});
    // A mapping has a few frames at most: one per look, and per group of rounds.
    uint16_t mask=0,key=0;
    for(;it!=frames_.end()&&it->mapping==mapping;++it){
        if(!wanted_[it->page])continue;
        if(it->mask!=mask){mask=it->mask;key=colour_key(line,mask);}
        if(it->colours==key)return &*it;
    }
    return nullptr;
}
}
