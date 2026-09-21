// Host analysis (SOR_EXTRACT_FRAMES=dir): every distinct object frame drawn
// during a replay, as the original's pixels around the object's anchor.
//
// Key: the frame's ROM mapping address and sor::colour_key of its CRAM line
// over the entries the object's art uses (art_catalog.hpp). Frames are stored
// unmirrored (a mirrored mapping is the same art flipped about the anchor).
// Output: dir/<mapping>_c<key>.pam (RGBA; alpha 0 where transparent) and
// dir/index.json (anchor, size, mask, CRAM line, object types, rounds, frames
// seen). Source for tools/make-enhanced-art.py.
//
// Whole animation sets: a replay shows only the frames its play happens to
// reach, but every object except the players keeps its art resident in VRAM.
// So once such an object has been on screen for a while (its art may still be
// decoding when it appears), every frame of its animation set (object +$04;
// layout in tools/extract-player-frames.py) is composed from the VRAM of that
// moment with the object's tile base (+$0E). A frame's "check" says how the
// set rendering compares with direct captures: confirmed, contradicted (both
// kept: <name>_set.pam), set_only, direct (never set-rendered) or
// direct_culled (the capture lacked pieces; the set rendering is used).
//
// The colour mask of a set is the union of the CRAM entries its frames use;
// an object without a set rendering (players: art loaded per frame) uses the
// union over the captures of each mapping. Masks are known only at the end, so
// captures are held by (mapping, whole CRAM line) until then.
#include "extract_frames.hpp"
#include "sprite_probe.hpp"
#include "art_catalog.hpp"
#include "VDPState.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <tuple>
namespace {
// One way a frame was seen. The game culls pieces outside the screen: the
// variant with the most pieces, then the one seen most often, is saved.
struct Variant {
    int width,height,anchorX,anchorY,pieces;   // anchor: pixel column/row of the object origin
    std::vector<uint8_t> rgba;
    bool operator<(const Variant &o) const {
        return std::tie(width,height,anchorX,anchorY,pieces,rgba)<std::tie(o.width,o.height,o.anchorX,o.anchorY,o.pieces,o.rgba);}
};
using Line=std::array<uint16_t,16>;
// Captures of one mapping under one whole CRAM line.
struct Capture {
    std::map<Variant,unsigned> direct,fromSet;
    std::set<int> types;
    unsigned seen=0,firstFrame=0,rounds=0,used=0;   // used: CRAM entries of its pixels
    int line=0;uint32_t set=0;
};
std::map<std::pair<uint32_t,Line>,Capture> captures;
std::map<uint32_t,unsigned> setMask;                       // animation set -> entries its frames use
struct SetSamples {unsigned last=0,count=0;};
std::map<std::tuple<uint32_t,int,uint16_t>,SetSamples> setSamples;   // (set, tile base, colours so far)
std::map<uint16_t,std::tuple<uint8_t,uint32_t,unsigned,unsigned>> shown;  // slot -> type, set, last frame, run
std::string directory;
std::vector<uint8_t> rom;
unsigned currentRound=0;
struct Writer {~Writer(){sor::extract_frames_finish();}} writer;
unsigned word(uint32_t a){return a+1<rom.size()?unsigned(rom[a]<<8|rom[a+1]):0u;}
uint32_t rgba(const VDPState &s,int palette,int index){
    const uint16_t c=s.cram_[palette*16+index];
    const auto v=[](unsigned n){return uint32_t((n&7)*255/7);};
    return v(c>>1)|v(c>>5)<<8|v(c>>9)<<16|0xFF000000u;
}
Line line_of(const VDPState &s,int line){Line l;for(int i=0;i<16;i++)l[i]=s.cram_[line*16+i]&0x0EEE;return l;}
// Pieces of a frame -> image. Piece: x,y relative to the anchor, size, attributes.
struct Piece{int x,y,w,h,attr;};
bool compose(const VDPState &s,const std::vector<Piece> &pieces,bool flip,Variant &out,unsigned &used){
    int x0=1<<30,y0=1<<30,x1=-(1<<30),y1=-(1<<30);
    for(const auto &p:pieces){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x+p.w);y1=std::max(y1,p.y+p.h);}
    const int width=x1-x0,height=y1-y0;
    if(pieces.empty()||width<=0||height<=0||width>512||height>512)return false;
    std::vector<uint8_t> image(size_t(width)*height*4,0);
    bool any=false;
    for(const auto &p:pieces){
        const bool hf=p.attr&0x800,vf=p.attr&0x1000;const int tile=p.attr&2047,pal=p.attr>>13&3,cells=p.h/8;
        for(int py=0;py<p.h;py++)for(int px=0;px<p.w;px++){
            const int sx=hf?p.w-1-px:px,sy=vf?p.h-1-py:py;
            const int address=((tile+(sx/8)*cells+sy/8)*32+(sy&7)*4+(sx&7)/2)&0xFFFF;
            const int index=(sx&1)?s.vram_[address]&15:s.vram_[address]>>4;
            if(!index)continue;
            // Earlier records are in front: keep the first opaque pixel.
            int ix=p.x+px-x0;const int iy=p.y+py-y0;
            if(flip)ix=width-1-ix;   // store unmirrored
            auto *d=&image[(size_t(iy)*width+ix)*4];
            if(d[3])continue;
            const uint32_t c=rgba(s,pal,index);
            d[0]=c&255;d[1]=c>>8&255;d[2]=c>>16&255;d[3]=255;any=true;used|=1u<<index;
        }
    }
    // Mirrored frames are stored flipped about the anchor.
    out=Variant{width,height,flip?width+x0:-x0,-y0,int(pieces.size()),std::move(image)};
    return any;
}
Capture &capture_of(const VDPState &s,uint32_t mapping,int line,const sor::ProbedObject &obj,unsigned frameNumber){
    auto &c=captures[{mapping,line_of(s,line)}];
    if(c.types.empty()){c.firstFrame=frameNumber;c.line=line;}
    c.types.insert(obj.type);
    if(currentRound)c.rounds|=1u<<(currentRound-1);
    return c;
}
// Players (and type $07, which draws from the players' sets) load art per frame.
bool has_resident_set(const sor::ProbedObject &obj){
    return !rom.empty()&&obj.type!=1&&obj.type!=7&&obj.set>=0x200&&obj.set<rom.size();
}
// Every frame of an object's animation set, from the VRAM of this moment.
void extract_set(const VDPState &s,const sor::ProbedObject &obj,int line,unsigned frameNumber){
    const unsigned first=word(obj.set);
    if(first<2||first>1024||(first&1))return;
    std::set<uint32_t> records;
    for(unsigned i=0;i<first/2;i++){
        const uint32_t anim=obj.set+word(obj.set+i*2);
        if(anim+2>=rom.size())continue;
        const unsigned count=rom[anim];
        if(!count||count>64)continue;
        for(unsigned f=0;f<count;f++)records.insert(anim+(word(anim+2+f*2)&0x7FFF));
    }
    // Not every object's +$04 is an animation set (cutscene objects keep other
    // data there): the frame on screen must be one of the set's, and a set has
    // at most a couple of hundred.
    if(!records.count(obj.mapping)||records.size()>240)return;
    for(uint32_t record:records){
        if(record+5>=rom.size()||rom[record]>40)continue;
        const unsigned n=rom[record]+1u;
        if(record+5+n*5>rom.size())continue;
        std::vector<Piece> pieces;
        for(unsigned i=0;i<n;i++){
            const uint8_t *e=&rom[record+5+i*5];
            pieces.push_back({int8_t(e[4]),int8_t(e[0]),((e[1]>>2&3)+1)*8,((e[1]&3)+1)*8,(int(e[2]<<8|e[3])+obj.tileBase)&0xFFFF});
        }
        // A frame on another palette line than the object's displayed one
        // belongs with that line's colours.
        const int frameLine=pieces[0].attr>>13&3;
        Variant v;unsigned used=0;
        if(!compose(s,pieces,false,v,used))continue;
        auto &c=capture_of(s,record,frameLine,obj,frameNumber);
        c.set=obj.set;c.used|=used;setMask[obj.set]|=used;
        c.fromSet[std::move(v)]++;
    }
    (void)line;
}
void write_pam(const std::string &path,const Variant &v){
    if(FILE *pam=fopen(path.c_str(),"wb")){
        fprintf(pam,"P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",v.width,v.height);
        fwrite(v.rgba.data(),1,v.rgba.size(),pam);fclose(pam);
    }
}
}
namespace sor {
void extract_frames_round(unsigned round){currentRound=round;}
void extract_frames_rom(const char *path){
    if(FILE *f=fopen(path,"rb")){
        fseek(f,0,SEEK_END);rom.resize(size_t(ftell(f)));fseek(f,0,SEEK_SET);
        if(fread(rom.data(),1,rom.size(),f)!=rom.size())rom.clear();
        fclose(f);
    }
}
void extract_frames(const VDPState &s,unsigned frameNumber){
    if(directory.empty()){const char *d=std::getenv("SOR_EXTRACT_FRAMES");if(!d)return;directory=d;}
    // Rounds only: title, menu and cutscene objects are not replaced.
    if(!currentRound)return;
    const SpriteBuild *build=sprite_probe().displayed(s);
    if(!build)return;
    const int base=s.satBase();
    for(unsigned o=0;o<build->count;o++){
        const auto &obj=build->objects[o];
        std::vector<Piece> pieces;
        for(unsigned r=obj.first;r<unsigned(obj.first+obj.count);r++){
            const uint8_t *e=s.vram_+((base+r*8)&0xFFFF);
            pieces.push_back({((e[6]&1)<<8|e[7])-obj.x,((e[0]&3)<<8|e[1])-obj.y,((e[2]>>2&3)+1)*8,((e[2]&3)+1)*8,e[4]<<8|e[5]});
        }
        if(pieces.empty())continue;
        const int line=pieces[0].attr>>13&3;
        if(has_resident_set(obj)){
            // How long this object has been on screen without a break.
            auto &[type,set,last,run]=shown[obj.slot];
            run=type==obj.type&&set==obj.set&&last+1>=frameNumber?run+1:1;
            type=obj.type;set=obj.set;last=frameNumber;
            auto &sample=setSamples[{obj.set,obj.tileBase,colour_key(s.cram_+line*16,uint16_t(setMask[obj.set]?setMask[obj.set]:0xFFFE))}];
            if(run>=45&&sample.count<3&&(!sample.count||frameNumber>=sample.last+120)){
                sample={frameNumber,sample.count+1};
                extract_set(s,obj,line,frameNumber);
            }
        }
        Variant v;unsigned used=0;
        compose(s,pieces,obj.flip,v,used);
        auto &c=capture_of(s,obj.mapping,line,obj,frameNumber);
        if(has_resident_set(obj))c.set=obj.set;
        c.used|=used;c.seen++;
        c.direct[std::move(v)]++;
    }
}
void extract_frames_finish(){
    if(directory.empty())return;
    // Masks: the set's, else the union over the mapping's captures.
    std::map<uint32_t,unsigned> mappingMask;
    for(const auto &[key,c]:captures)mappingMask[key.first]|=c.used;
    struct Frame {std::map<Variant,unsigned> direct,fromSet;std::set<int> types;unsigned seen=0,firstFrame=~0u,rounds=0,mask=0;int line=0;Line cram{};};
    std::map<std::pair<uint32_t,uint16_t>,Frame> frames;
    for(const auto &[key,c]:captures){
        unsigned mask=c.set&&setMask.count(c.set)?setMask[c.set]:mappingMask[key.first];
        mask&=0xFFFE;
        auto &f=frames[{key.first,colour_key(key.second.data(),uint16_t(mask))}];
        if(c.firstFrame<f.firstFrame){f.firstFrame=c.firstFrame;f.cram=key.second;f.line=c.line;}
        f.mask=mask;f.seen+=c.seen;f.rounds|=c.rounds;f.types.insert(c.types.begin(),c.types.end());
        for(const auto &[v,n]:c.direct)f.direct[v]+=n;
        for(const auto &[v,n]:c.fromSet)f.fromSet[v]+=n;
    }
    FILE *out=fopen((directory+"/index.json").c_str(),"w");if(!out)return;
    fprintf(out,"{\"frames\": [\n");
    bool first=true;
    unsigned confirmed=0,contradicted=0,setOnly=0;
    for(const auto &[key,f]:frames){
        const Variant *best=nullptr;unsigned most=0;
        for(const auto &[v,count]:f.direct)
            if(!best||v.pieces>best->pieces||(v.pieces==best->pieces&&count>most)){best=&v;most=count;}
        // The set rendering (most frequent sample) fills in frames never drawn
        // and replaces captures that were culled at the screen edge.
        const Variant *whole=nullptr;unsigned wholeMost=0;
        for(const auto &[v,count]:f.fromSet)if(count>wholeMost){whole=&v;wholeMost=count;}
        char name[40];snprintf(name,sizeof name,"/%06X_c%04X",key.first,key.second);
        const char *check="direct";
        if(whole){
            if(!best){setOnly++;check="set_only";}
            else if(best->pieces==whole->pieces){
                if(best->rgba==whole->rgba){confirmed++;check="confirmed";}
                else{contradicted++;check="contradicted";write_pam(directory+name+"_set.pam",*whole);}
            }else check="direct_culled";
            if(!best||best->pieces<whole->pieces)best=whole;
        }
        if(!best||best->rgba.empty())continue;
        write_pam(directory+name+".pam",*best);
        fprintf(out,"%s  {\"mapping\": \"%06X\", \"colours\": \"%04X\", \"mask\": %u, \"palette\": %d, \"cram\": [",first?"":",\n",key.first,key.second,f.mask,f.line);
        for(int i=0;i<16;i++)fprintf(out,"%s%u",i?",":"",f.cram[i]);
        fprintf(out,"], \"width\": %d, \"height\": %d, \"anchor\": [%d, %d], \"seen\": %u, \"first_frame\": %u, \"check\": \"%s\", \"rounds\": %u, \"types\": [",
                best->width,best->height,best->anchorX,best->anchorY,f.seen,f.firstFrame,check,f.rounds);
        bool t0=true;for(int t:f.types){fprintf(out,"%s%d",t0?"":", ",t);t0=false;}
        fprintf(out,"]}");first=false;
    }
    fprintf(out,"\n],\n\"set_check\": {\"confirmed\": %u, \"contradicted\": %u, \"set_only\": %u}}\n",confirmed,contradicted,setOnly);fclose(out);
    directory.clear();
}
}
