#include "vdp_scene.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace sor {
namespace {
int scroll(const VDPState &s,int plane,int y){
    int row=s.hscrollMode()==3?y:(s.hscrollMode()==2?(y&~7):0);
    if(s.hscrollMode()==1)return 0;
    unsigned a=(s.hscrollBase()+row*4+plane*2)&65535;
    return int16_t((s.vram_[a]<<8)|s.vram_[(a+1)&65535]);
}
void window_span(const VDPState &s,int y,int &a,int &b){
    a=b=0;
    int vy=s.windowVPos(),hx=s.windowHPos()*16;
    if(vy>0 && (s.windowDown()?y/8>=vy:y/8<vy)){b=s.activeWidth();return;}
    if(hx>0){int edge=std::min(hx,s.activeWidth());if(s.windowRight()){a=edge;b=s.activeWidth();}else b=edge;}
}
uint16_t entry(const VDPState &s,int a){return (s.vram_[a&65535]<<8)|s.vram_[(a+1)&65535];}
bool unchanged_region(const VDPState &a,const VDPState &b,int base,int length){
    if(length>=65536)return !std::memcmp(a.vram_,b.vram_,65536);
    base&=65535;int first=std::min(length,65536-base);
    return !std::memcmp(a.vram_+base,b.vram_+base,first)
        && (first==length || !std::memcmp(a.vram_,b.vram_,length-first));
}
bool same_geometry_regs(const VDPState &a,const VDPState &b){
    for(unsigned i:{1u,2u,3u,4u,11u,12u,13u,16u,17u,18u})if(a.regs_[i]!=b.regs_[i])return false;
    return true;
}
bool same_render_regs(const VDPState &a,const VDPState &b){
    return same_geometry_regs(a,b) && a.regs_[0]==b.regs_[0]
        && a.regs_[5]==b.regs_[5] && a.regs_[7]==b.regs_[7];
}
}
uint16_t VdpScene::rgb1555(unsigned r,unsigned g,unsigned b){
    return 0x8000|((r*255/7>>3)<<10)|((g*255/7>>3)<<5)|(b*255/7>>3);
}
bool VdpScene::buildCached(VDPState &s,VDPRenderer &){
    reused=cacheValid && same_render_regs(s,previous)
        && !std::memcmp(s.vram_,previous.vram_,sizeof(s.vram_))
        && !std::memcmp(s.cram_,previous.cram_,sizeof(s.cram_))
        && !std::memcmp(s.vsram_,previous.vsram_,sizeof(s.vsram_))
        && !std::memcmp(s.sat_,previous.sat_,sizeof(s.sat_));
    if(reused){
        planesReused=true;
        s.status_|=spriteFlags;
        if(s.displayEnabled())s.vCounter_=height-1;
        return true;
    }
    int mapBytes=s.planeWidthCells()*s.planeHeightCells()*2;
    bool geometrySame=cacheValid && same_geometry_regs(s,previous)
        && !std::memcmp(s.vsram_,previous.vsram_,sizeof(s.vsram_))
        && unchanged_region(s,previous,s.planeABase(),mapBytes)
        && unchanged_region(s,previous,s.planeBBase(),mapBytes)
        && unchanged_region(s,previous,s.windowBase(),(s.h40Mode()?64:32)*32*2)
        && unchanged_region(s,previous,s.hscrollBase(),s.hscrollMode()==0?4:s.activeHeight()*4);
    const auto status=s.status_;s.status_&=~0x60;
    cacheValid=buildImpl(s,geometrySame);spriteFlags=s.status_&0x60;s.status_|=status;
    if(cacheValid)previous=s;
    return cacheValid;
}
bool VdpScene::build(VDPState &s,VDPRenderer &){
    cacheValid=false;
    return buildImpl(s,false);
}
bool VdpScene::buildImpl(VDPState &s,bool keepPlanes){
    planesReused=keepPlanes;
    if(!keepPlanes)count=0;
    width=s.activeWidth();height=s.activeHeight();
    if(s.interlaced()||s.shadowHighlightEnabled()||s.vscrollMode()!=0||height>256)return false;
    unsigned mask=s.fullColorPaletteEnabled()?7:1;
    for(int i=0;i<64;i++){auto c=s.cram_[i];colors[i]=rgb1555((c>>1)&mask,(c>>5)&mask,(c>>9)&mask);}
    background=s.displayEnabled()?colors[s.bgColorPalette()*16+s.bgColorIndex()]:0x8000;
    for(int p=0;p<2;p++){
        if(spriteBottom[p]>spriteTop[p])
            std::memset(sprites[p]+spriteTop[p]*512,0,(spriteBottom[p]-spriteTop[p])*1024);
        spriteTop[p]=256;spriteBottom[p]=0;
    }
    if(!s.displayEnabled())return true;
    if(!keepPlanes){plane(s,1);plane(s,0);window(s);}
    spriteLayers(s);
    s.vCounter_=height-1;
    return true;
}
void VdpScene::spriteLayers(VDPState &s){
    // Traverse the linked SAT once per frame. Per-line counters preserve the
    // VDP's evaluation limits even for transparent or off-screen sprites.
    struct Row {uint16_t pixels=0;uint8_t count=0;bool seenX=false,masked=false,done=false;};
    Row rows[256]{};
    const int limit=s.h40Mode()?20:16,base=s.satBase();
    int index=0;
    for(int ordinal=0;ordinal<VDPState::SAT_MAX_SPRITES;ordinal++){
        const int shadow=index*8,addr=base+shadow;
        if(addr+7>=VDPState::VRAM_SIZE)break;
        const int y=((s.sat_[shadow]&3)<<8|s.sat_[shadow+1])-128;
        const int cellsH=(s.sat_[shadow+2]&3)+1;
        const int w=(((s.sat_[shadow+2]>>2)&3)+1)*8,h=cellsH*8;
        const int link=s.sat_[shadow+3]&127;
        const unsigned attr=entry(s,addr+4);
        const int rawX=((s.vram_[addr+6]&1)<<8)|s.vram_[addr+7],x=rawX-128;
        const bool flipX=attr&0x800,flipY=attr&0x1000;
        const int layer=(attr>>15)&1,palette=(attr>>13)&3,tile=attr&2047;
        for(int line=std::max(0,y);line<std::min(height,y+h);line++){
            auto &row=rows[line];
            if(row.done)continue;
            if(rawX)row.seenX=true;else if(row.seenX)row.masked=true;
            if(row.masked)continue;
            if(++row.count>limit){s.status_|=0x40;row.done=true;continue;}
            row.pixels+=w;
            const int start=std::max(0,x);
            int end=std::min(width,x+w);
            if(row.pixels>width){end=std::max(start,end-(row.pixels-width));s.status_|=0x40;}
            const int py=flipY?h-1-(line-y):line-y;
            const int tileRow=py/8,pixelRow=py&7;
            auto *dest=sprites[layer]+line*512;
            const auto *other=sprites[1-layer]+line*512;
            bool written=false;
            for(int screenX=start;screenX<end;){
                const int px=flipX?w-1-(screenX-x):screenX-x;
                const int run=std::min(end-screenX,flipX?(px&7)+1:8-(px&7));
                const int address=(tile+(px/8)*cellsH+tileRow)*32+pixelRow*4;
                if(address>VDPState::VRAM_SIZE-4){screenX+=run;continue;}
                // Decode one packed tile row, then walk its nibbles. Tile
                // addressing, facing and clipping are constant across this run.
                uint32_t bits=(uint32_t(s.vram_[address])<<24)|(uint32_t(s.vram_[address+1])<<16)|
                              (uint32_t(s.vram_[address+2])<<8)|s.vram_[address+3];
                bits=flipX?bits>>((7-(px&7))*4):bits<<((px&7)*4);
                if(!bits){screenX+=run;continue;}
                for(int i=0;i<run;i++,screenX++){
                    const unsigned color=flipX?bits&15:bits>>28;
                    bits=flipX?bits>>4:bits<<4;
                    if(!color)continue;
                    if(dest[screenX]||other[screenX])s.status_|=0x20;
                    else {dest[screenX]=colors[palette*16+color];written=true;}
                }
            }
            if(written){spriteTop[layer]=std::min(spriteTop[layer],line);spriteBottom[layer]=std::max(spriteBottom[layer],line+1);}
            if(row.pixels>=width)row.done=true;
        }
        if(!link||link>=VDPState::SAT_MAX_SPRITES)break;
        index=link;
    }
}
void VdpScene::add(uint16_t e,int x,int y,int w,int h,int px,int py,int lowDepth){
    if(count==MAX_QUADS)throw std::runtime_error("VDP scene quad bound exceeded");
    auto &q=quads[count++];q.tile=e&2047;q.palette=(e>>13)&3;q.depth=lowDepth+((e&0x8000)?3:0);
    q.x=x;q.y=y;q.w=w;q.h=h;
    q.u0=(e&0x800)?8-px:px;q.u1=(e&0x800)?8-px-w:px+w;
    q.v0=(e&0x1000)?8-py:py;q.v1=(e&0x1000)?8-py-h:py+h;
}
void VdpScene::plane(const VDPState &s,int p){
    int v=int16_t(s.vsram_[p]),xm=s.planeWidthCells()*8-1,ym=s.planeHeightCells()*8-1;
    int base=p?s.planeBBase():s.planeABase();
    const int cells=s.planeWidthCells();
    for(int y=0;y<height;){
        int hs=scroll(s,p,y),sy=(y+v)&ym,rows=std::min(8-(sy&7),height-y),wa,wb;
        window_span(s,y,wa,wb);
        for(int i=1;i<rows;i++){
            int a,b;window_span(s,y+i,a,b);
            if(scroll(s,p,y+i)!=hs || (!p&&(a!=wa||b!=wb))){rows=i;break;}
        }
        for(int x=0;x<width;){
            int sx=(x-hs)&xm,n=std::min(8-(sx&7),width-x);
            if(!p && x<wa)n=std::min(n,wa-x);
            if(!p && x>=wa && x<wb){x=wb;continue;}
            auto e=entry(s,base+((sy>>3)*cells+(sx>>3))*2);
            add(e,x,y,n,rows,sx&7,sy&7,p?1:2);x+=n;
        }
        y+=rows;
    }
}
void VdpScene::window(const VDPState &s){
    for(int y=0;y<height;){
        int a,b;window_span(s,y,a,b);int rows=std::min(8-(y&7),height-y);
        for(int i=1;i<rows;i++){int c,d;window_span(s,y+i,c,d);if(a!=c||b!=d){rows=i;break;}}
        for(int x=a;x<b;){int n=std::min(8-(x&7),b-x);auto e=entry(s,s.windowBase()+((y>>3)*(s.h40Mode()?64:32)+(x>>3))*2);
            add(e,x,y,n,rows,x&7,y&7,2);x+=n;}
        y+=rows;
    }
}
void raster_scene(const VdpScene &scene,const VDPState &s,uint16_t *out){
    // Explicit depths reproduce Genesis plane/sprite priority, independent of submission order.
    std::fill_n(out,320*240,scene.background);
    for(int depth=1;depth<=6;depth++){
        if(depth==3||depth==6){const auto *sp=scene.sprites[depth==6];
            for(int y=0;y<scene.height;y++)for(int x=0;x<scene.width;x++)if(sp[y*512+x]&0x8000)out[y*320+x]=sp[y*512+x];
        }
        for(size_t i=0;i<scene.count;i++){const auto &q=scene.quads[i];if(q.depth!=depth)continue;
            for(int y=0;y<q.h;y++)for(int x=0;x<q.w;x++){
                int u=q.u1>q.u0?q.u0+x:q.u0-1-x,v=q.v1>q.v0?q.v0+y:q.v0-1-y;
                uint8_t b=s.vram_[q.tile*32+v*4+u/2];unsigned c=(u&1)?b&15:b>>4;
                if(c)out[(q.y+y)*320+q.x+x]=scene.colors[q.palette*16+c];
            }
        }
    }
}
}
