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
void ArtCatalog::select(unsigned round,unsigned characters,bool inbetweens){
    for(size_t i=0;i<pages_.size();i++){
        const auto &p=pages_[i];const unsigned character=p.character&0x7F;
        wanted_[i]=(inbetweens||!p.inbetween())
            &&(!round||((p.rounds>>(round-1)&1)&&(!character||(characters>>character&1))));
    }
}
bool ArtCatalog::inflate(const ArtPage &page,uint8_t *out) const{
    uLongf n=uLongf(page.width)*page.height;
    return page.packed&&uncompress(out,&n,page.packed,page.packedSize)==Z_OK&&n==uLongf(page.width)*page.height;
}
bool ArtCatalog::load(const uint8_t *data,size_t size,bool inflatePages){
    frames_.clear();pages_.clear();palette_.clear();inflated_.clear();
    Reader r{data,data+size};
    if(!r.has(20)||std::memcmp(data,"SORART0",7)||data[7]<'4'||data[7]>'6')return false;
    const bool hasFrom=data[7]>='5',hasLine=data[7]>='6';
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
        if(!r.has(22u+(hasFrom?4:0)+(hasLine?32:0)))return false;
        ArtFrame f{};
        f.mapping=r.u32();f.colours=r.u16();f.mask=r.u16();f.page=r.u16();f.u=r.u16();f.v=r.u16();f.w=r.u16();f.h=r.u16();
        f.anchorX=int16_t(r.u16());f.anchorY=int16_t(r.u16());f.from=hasFrom?r.u32():0;
        if(hasLine)for(auto &c:f.line)c=r.u16();
        if(f.page>=pages_.size()||f.u+f.w>pages_[f.page].width||f.v+f.h>pages_[f.page].height)return false;
        frames_.push_back(f);
    }
    wanted_.assign(pages_.size(),1);
    std::stable_sort(frames_.begin(),frames_.end(),[](const ArtFrame &a,const ArtFrame &b){return a.from!=b.from?a.from<b.from:a.mapping<b.mapping;});
    return true;
}
namespace {
// line = look changed by one step k per channel (subtracted: a fade; added: a
// flash), over the masked entries? Then the tint that does the same to art.
bool fade_of(const uint16_t *line,const ArtFrame &f,ArtTint &tint){
    bool any=false;
    for(int i=1;i<16;i++)any|=(f.mask>>i&1)&&f.line[i];
    if(!any)return false;                       // no colours recorded (older package), or black art
    for(int direction=-1;direction<=1;direction+=2){
        ArtTint t;bool fits=true,changed=false;
        for(int c=0;c<3&&fits;c++){
            const int shift=1+c*4;              // CRAM: ----bbb-ggg-rrr-; channel values 0-7
            int k=-1;
            for(int step=0;step<8&&k<0;step++){
                bool ok=true;
                for(int i=1;i<16&&ok;i++)if(f.mask>>i&1){
                    const int was=f.line[i]>>shift&7,is=line[i]>>shift&7;
                    ok=is==(direction<0?std::max(was-step,0):std::min(was+step,7));
                }
                if(ok)k=step;
            }
            if(k<0){fits=false;break;}
            changed|=k>0;
            if(direction>0)t.offset[c]=uint8_t(k*255/7);
            else if(k){
                // A multiplication stands in for the subtraction: the ratio of
                // the line's brightness in this channel, after and before.
                int before=0,after=0;
                for(int i=1;i<16;i++)if(f.mask>>i&1){before+=f.line[i]>>shift&7;after+=line[i]>>shift&7;}
                t.scale[c]=uint8_t(before?after*255/before:255);
            }
        }
        if(fits&&changed){tint=t;return true;}
    }
    return false;
}
}
const ArtFrame *ArtCatalog::find(uint32_t from,uint32_t mapping,const uint16_t *line,ArtTint *tint) const{
    auto first=std::lower_bound(frames_.begin(),frames_.end(),mapping,[from](const ArtFrame &f,uint32_t m){return f.from!=from?f.from<from:f.mapping<m;});
    // A mapping has a few frames at most: one per look, and per group of rounds.
    uint16_t mask=0,key=0;
    if(tint)*tint=ArtTint{};
    for(auto it=first;it!=frames_.end()&&it->mapping==mapping&&it->from==from;++it){
        if(!wanted_[it->page])continue;
        if(it->mask!=mask){mask=it->mask;key=colour_key(line,mask);}
        if(it->colours==key)return &*it;
    }
    if(tint)for(auto it=first;it!=frames_.end()&&it->mapping==mapping&&it->from==from;++it)
        if(wanted_[it->page]&&fade_of(line,*it,*tint))return &*it;
    return nullptr;
}
}
