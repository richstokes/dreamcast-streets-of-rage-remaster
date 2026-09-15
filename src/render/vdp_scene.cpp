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
bool VdpScene::buildCached(VDPState &s,VDPRenderer &renderer){
    reused=cacheValid && same_render_regs(s,previous)
        && !std::memcmp(s.vram_,previous.vram_,sizeof(s.vram_))
        && !std::memcmp(s.cram_,previous.cram_,sizeof(s.cram_))
        && !std::memcmp(s.vsram_,previous.vsram_,sizeof(s.vsram_))
        && !std::memcmp(s.sat_,previous.sat_,sizeof(s.sat_));
    if(reused){
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
    cacheValid=buildImpl(s,renderer,geometrySame);spriteFlags=s.status_&0x60;s.status_|=status;
    if(cacheValid)previous=s;
    return cacheValid;
}
bool VdpScene::build(VDPState &s,VDPRenderer &renderer){
    cacheValid=false;
    return buildImpl(s,renderer,false);
}
bool VdpScene::buildImpl(VDPState &s,VDPRenderer &renderer,bool keepPlanes){
    if(!keepPlanes)count=0;
    width=s.activeWidth();height=s.activeHeight();
    if(s.interlaced()||s.shadowHighlightEnabled()||s.vscrollMode()!=0||height>256)return false;
    unsigned mask=s.fullColorPaletteEnabled()?7:1;
    for(int i=0;i<64;i++){auto c=s.cram_[i];colors[i]=rgb1555((c>>1)&mask,(c>>5)&mask,(c>>9)&mask);}
    background=s.displayEnabled()?colors[s.bgColorPalette()*16+s.bgColorIndex()]:0x8000;
    std::memset(sprites,0,sizeof(sprites));
    if(!s.displayEnabled())return true;
    if(!keepPlanes){plane(s,1);plane(s,0);window(s);}
    for(int y=0;y<height;y++){
        s.vCounter_=y;
        const auto *line=renderer.nativeSpriteLine(y);
        for(int x=0;x<width;x++)if(line[x].opaque)
            sprites[line[x].priority?1:0][y*512+x]=colors[line[x].palette*16+line[x].colorIndex];
    }
    return true;
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
