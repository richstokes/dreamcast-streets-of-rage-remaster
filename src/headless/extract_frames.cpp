// Host analysis (SOR_EXTRACT_FRAMES=dir): every distinct object frame drawn
// during a replay, as the original's pixels around the object's anchor.
// Frames are keyed by their ROM mapping address and palette line and stored
// unmirrored (a mirrored mapping is the same art flipped about the anchor).
// Output: dir/<mapping>_p<palette>.pam (RGBA; alpha 0 where transparent) and
// dir/index.json (anchor, size, object types, frames seen). Source for the
// placeholder art and for replacement-art templates (tools/make-placeholder-art.py).
#include "extract_frames.hpp"
#include "sprite_probe.hpp"
#include "VDPState.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <tuple>
namespace {
// One way a frame was seen. The game culls pieces outside the screen, and
// palette fades and flashes recolour a frame: the variant with the most pieces,
// then the one seen most often, is saved.
struct Variant {
    int width,height,anchorX,anchorY,pieces;   // anchor: pixel column/row of the object origin
    std::vector<uint8_t> rgba;
    bool operator<(const Variant &o) const {
        return std::tie(width,height,anchorX,anchorY,pieces,rgba)<std::tie(o.width,o.height,o.anchorX,o.anchorY,o.pieces,o.rgba);}
};
struct Frame {
    std::map<Variant,unsigned> variants;
    std::set<int> types;
    unsigned seen=0,firstFrame=0;
};
std::map<std::pair<uint32_t,int>,Frame> frames;
std::string directory;
struct Writer {~Writer(){sor::extract_frames_finish();}} writer;
uint32_t rgba(const VDPState &s,int palette,int index){
    const uint16_t c=s.cram_[palette*16+index];
    const auto v=[](unsigned n){return uint32_t((n&7)*255/7);};
    return v(c>>1)|v(c>>5)<<8|v(c>>9)<<16|0xFF000000u;
}
}
namespace sor {
void extract_frames(const VDPState &s,unsigned frameNumber){
    if(directory.empty()){const char *d=std::getenv("SOR_EXTRACT_FRAMES");if(!d)return;directory=d;}
    const SpriteBuild *build=sprite_probe().displayed(s);
    if(!build)return;
    const int base=s.satBase();
    for(unsigned o=0;o<build->count;o++){
        const auto &obj=build->objects[o];
        // Bounds of the object's pieces relative to its anchor.
        int x0=1<<30,y0=1<<30,x1=-(1<<30),y1=-(1<<30),palette=-1;
        struct Piece{int x,y,w,h,attr;};std::vector<Piece> pieces;
        for(unsigned r=obj.first;r<unsigned(obj.first+obj.count);r++){
            const uint8_t *e=s.vram_+((base+r*8)&0xFFFF);
            const int y=((e[0]&3)<<8|e[1])-obj.y,x=((e[6]&1)<<8|e[7])-obj.x;
            const int w=((e[2]>>2&3)+1)*8,h=((e[2]&3)+1)*8,attr=e[4]<<8|e[5];
            pieces.push_back({x,y,w,h,attr});
            x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+w);y1=std::max(y1,y+h);
            if(palette<0)palette=attr>>13&3;
        }
        if(pieces.empty())continue;
        const int width=x1-x0,height=y1-y0;
        std::vector<uint8_t> image(size_t(width)*height*4,0);
        for(const auto &p:pieces){
            const bool hf=p.attr&0x800,vf=p.attr&0x1000;const int tile=p.attr&2047,pal=p.attr>>13&3,cells=p.h/8;
            for(int py=0;py<p.h;py++)for(int px=0;px<p.w;px++){
                const int sx=hf?p.w-1-px:px,sy=vf?p.h-1-py:py;
                const int address=((tile+(sx/8)*cells+sy/8)*32+(sy&7)*4+(sx&7)/2)&0xFFFF;
                const int index=(sx&1)?s.vram_[address]&15:s.vram_[address]>>4;
                if(!index)continue;
                // Earlier records are in front: keep the first opaque pixel.
                int ix=p.x+px-x0;const int iy=p.y+py-y0;
                if(obj.flip)ix=width-1-ix;   // store unmirrored
                auto *d=&image[(size_t(iy)*width+ix)*4];
                if(d[3])continue;
                const uint32_t c=rgba(s,pal,index);
                d[0]=c&255;d[1]=c>>8&255;d[2]=c>>16&255;d[3]=255;
            }
        }
        auto &f=frames[{obj.mapping,palette}];
        f.types.insert(obj.type);
        if(!f.seen++)f.firstFrame=frameNumber;
        // Mirrored frames are stored flipped about the anchor.
        f.variants[Variant{width,height,obj.flip?width+x0:-x0,-y0,int(pieces.size()),std::move(image)}]++;
    }
}
void extract_frames_finish(){
    if(directory.empty())return;
    FILE *out=fopen((directory+"/index.json").c_str(),"w");if(!out)return;
    fprintf(out,"{\"frames\": [\n");
    bool first=true;
    for(const auto &[key,f]:frames){
        const Variant *best=nullptr;unsigned most=0;
        for(const auto &[v,count]:f.variants)
            if(!best||v.pieces>best->pieces||(v.pieces==best->pieces&&count>most)){best=&v;most=count;}
        if(!best)continue;
        char name[32];snprintf(name,sizeof name,"/%06X_p%d.pam",key.first,key.second);
        if(FILE *pam=fopen((directory+name).c_str(),"wb")){
            fprintf(pam,"P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",best->width,best->height);
            fwrite(best->rgba.data(),1,best->rgba.size(),pam);fclose(pam);
        }
        fprintf(out,"%s  {\"mapping\": \"%06X\", \"palette\": %d, \"width\": %d, \"height\": %d, \"anchor\": [%d, %d], \"seen\": %u, \"first_frame\": %u, \"variants\": %zu, \"types\": [",
                first?"":",\n",key.first,key.second,best->width,best->height,best->anchorX,best->anchorY,f.seen,f.firstFrame,f.variants.size());
        bool t0=true;for(int t:f.types){fprintf(out,"%s%d",t0?"":", ",t);t0=false;}
        fprintf(out,"]}");first=false;
    }
    fprintf(out,"\n]}\n");fclose(out);
    directory.clear();
}
}
