#include "diagnostics.hpp"
#include <kos.h>
#include <cstring>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include "vdp_scene.hpp"
#include "pvr_tiles.hpp"
#include "equal_bytes.hpp"
#include "cheats.hpp"
#include "title_caption.hpp"
#include "art_catalog.hpp"
#include "platform.hpp"
#include "replay.hpp"
#include "sor_audio_config.hpp"
#include <vector>
namespace {
std::unique_ptr<sor::VdpScene> scene;
pvr_ptr_t tiles=nullptr,spriteTexture[2]{};
pvr_poly_hdr_t tileHeaders[8192],spriteHeaders[2];
pvr_ptr_t cheatHintTexture=nullptr;
pvr_poly_hdr_t cheatHintHeader;
pvr_ptr_t titleTextures[sor::TitleCaption::regions.size()]{};
pvr_poly_hdr_t titleHeaders[sor::TitleCaption::regions.size()];
uint16_t titleTextureKey=0;
alignas(32) uint8_t previousTiles[65536]{};
uint8_t valid[2048]{};
alignas(32) uint16_t previousColors[64]{};
bool opaque[2048]{},planeTiles[2048]{};
uint32_t frames=0;
struct alignas(32) Packet {pvr_poly_hdr_t header;pvr_vertex_t vertices[4];};
constexpr size_t maxTileQuads=6000;
static_assert(sizeof(Packet)==160);
Packet packets[maxTileQuads+2];
size_t packetCount=0;
bool packetsValid=false,packetsEnhanced=false;
// Enhanced graphics: hardware sprite cells and replacement-art frames, one
// quad each, rebuilt when the scene changes (docs/REMASTER.md).
constexpr size_t maxSpritePackets=sor::VdpScene::MAX_SPRITE_TILES+80;
Packet spritePackets[maxSpritePackets];
size_t spritePacketCount=0;
sor::ArtCatalog art;
std::vector<uint8_t> artPackage;
std::vector<pvr_ptr_t> artTextures;
std::vector<pvr_poly_hdr_t> artHeaders;
int uploadedTop[2]{256,256},uploadedBottom[2]{};
void header(pvr_poly_hdr_t &h,pvr_ptr_t texture,int w,int hgt,bool linear,int palette=-1){
    pvr_poly_cxt_t c;
    // palette: -1 ARGB1555; -2, -3, -4: 8-bit indices into bank 1, 2, 3 (art); else a 4-bit bank.
    const int format=palette==-1 ? (PVR_TXRFMT_ARGB1555|(linear?PVR_TXRFMT_NONTWIDDLED:0))
                    : palette<=-2 ? (PVR_TXRFMT_PAL8BPP|PVR_TXRFMT_8BPP_PAL(-1-palette))
                    : (PVR_TXRFMT_PAL4BPP|PVR_TXRFMT_4BPP_PAL(palette));
    pvr_poly_cxt_txr(&c,PVR_LIST_PT_POLY,format,w,hgt,texture,PVR_FILTER_NONE);
    if(palette<=-2)c.gen.specular=PVR_SPECULAR_ENABLE;   // art: the offset colour carries flashes
    c.gen.culling=PVR_CULLING_NONE;
    c.depth.comparison=PVR_DEPTHCMP_GEQUAL;
    pvr_poly_compile(&h,&c);
}
void quad(Packet &packet,const pvr_poly_hdr_t &h,float x,float y,float w,float hgt,float z,float u0,float v0,float u1,float v1){
    packet={};packet.header=h;
    const float xx[]={x,x+w,x,x+w},yy[]={y,y,y+hgt,y+hgt};
    for(int i=0;i<4;i++){auto &v=packet.vertices[i];v.z=z;v.argb=0xffffffff;v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xx[i];v.y=yy[i];v.u=(i&1)?u1:u0;v.v=(i&2)?v1:v0;}

}
}
namespace {
void load_art(){
    const auto start=timer_us_gettime64();
    size_t embedded=0;const uint8_t *built_in=platform_embedded_art(embedded);
    if(FILE *f=fopen("/cd/SORART.PAK","rb")){
        fseek(f,0,SEEK_END);const long size=ftell(f);fseek(f,0,SEEK_SET);
        artPackage.resize(size_t(size));
        const bool read=fread(artPackage.data(),1,artPackage.size(),f)==artPackage.size();fclose(f);
        if(!read){sor_log("Enhanced art: /cd/SORART.PAK unreadable\n");artPackage.clear();}
    }
    // A package built into the executable is read where it lies, with no copy.
    const uint8_t *data=artPackage.empty()?built_in:artPackage.data();
    const size_t size=artPackage.empty()?embedded:artPackage.size();
    if(!data||!size){sor_log("Enhanced art: no package (original sprites drawn per cell)\n");return;}
    if(!art.load(data,size,false)){sor_log("Enhanced art: invalid package\n");art=sor::ArtCatalog();return;}
    // Art is 8-bit indices into palette banks 1-3 (entries 256-1023; the 4-bit
    // tile palettes use entries 0-63 of bank 0). The compressed package stays in main
    // RAM; pages are inflated and uploaded per round (load_round).
    for(size_t i=0;i<art.palettes().size();i++)pvr_set_pal_entry(256+i,art.palettes()[i]);
    artTextures.assign(art.pages().size(),nullptr);artHeaders.resize(art.pages().size());
    sor_log("Enhanced art: %zu frames in %zu pages, %zu package bytes\n",art.frames().size(),art.pages().size(),size);
}
// Load the art of the round and characters in play, in place of what was
// loaded. All of the game's art does not fit in PowerVR memory, and one inflated
// page at a time is all main RAM holds. Enhanced graphics do not use the
// software sprite layers, so their textures make room for art while it is on.
unsigned artRound=~0u,artCharacters=~0u;bool artEnhanced=false,artSmooth=false;
void load_selection(unsigned round,unsigned characters,bool enhanced,bool smooth){
    if(round==artRound&&characters==artCharacters&&enhanced==artEnhanced&&smooth==artSmooth)return;
    artRound=round;artCharacters=characters;artEnhanced=enhanced;artSmooth=smooth;
    const auto start=timer_us_gettime64();
    pvr_wait_ready();                         // the frame in flight still samples these textures
    if(scene)scene->invalidate();
    if(!enhanced||art.empty()){
        for(auto &p:artTextures)if(p){pvr_mem_free(p);p=nullptr;}
        for(int p=0;p<2;p++)if(!spriteTexture[p]){
            spriteTexture[p]=pvr_mem_malloc(512*256*2);
            if(!spriteTexture[p])throw std::runtime_error("PowerVR sprite layers unavailable");
            header(spriteHeaders[p],spriteTexture[p],512,256,true);
        }
        uploadedTop[0]=uploadedTop[1]=0;uploadedBottom[0]=uploadedBottom[1]=256;
        return;
    }
    for(auto &p:spriteTexture)if(p){pvr_mem_free(p);p=nullptr;}
    // In-between pages come last: they take only what the ordinary art leaves.
    art.select(round,characters,smooth);
    for(size_t i=0;i<artTextures.size();i++)
        if(artTextures[i]&&!art.wanted(i)){pvr_mem_free(artTextures[i]);artTextures[i]=nullptr;}
    size_t bytes=0,loaded=0,missing=0;
    std::vector<uint8_t> indices;
    for(size_t i=0;i<artTextures.size();i++){
        const auto &page=art.pages()[i];
        if(!art.wanted(i))continue;
        const size_t texels=size_t(page.width)*page.height;
        if(artTextures[i]){bytes+=texels;continue;}   // already there
        indices.resize(texels);
        pvr_ptr_t t=missing?nullptr:pvr_mem_malloc(texels);
        if(!t||!art.inflate(page,indices.data())){
            // Pages are stored most important first: the rest fall back to the original pieces.
            if(t)pvr_mem_free(t);
            art.setUnloaded(i);missing++;continue;
        }
        pvr_txr_load_ex(indices.data(),t,page.width,page.height,PVR_TXRLOAD_8BPP);
        artTextures[i]=t;header(artHeaders[i],t,page.width,page.height,true,-2-int(page.palette));loaded++;bytes+=texels;
    }
    sor_log("Enhanced art: round %u characters %x: %zu pages loaded, %zu did not fit, %zu PowerVR bytes in use; free %lu; %llu ms\n",round,characters,loaded,missing,bytes,
            (unsigned long)pvr_mem_available(),(unsigned long long)((timer_us_gettime64()-start)/1000));
    if(!replay_active())sor_flush_log();
}
unsigned gameRound=1,gameCharacters=0;
}
// Followed outside play too: the round intro is when a round's art should load.
void dc_renderer_game_state(unsigned round,unsigned characters){gameRound=round;gameCharacters=characters;}
void dc_renderer_init(){
    scene=std::make_unique<sor::VdpScene>();
    tiles=pvr_mem_malloc(2048*32);
    pvr_set_pal_format(PVR_PAL_ARGB1555);
    for(int p=0;p<2;p++)spriteTexture[p]=pvr_mem_malloc(512*256*2);
    if(!tiles||!spriteTexture[0]||!spriteTexture[1])throw std::runtime_error("PowerVR texture budget exhausted");
    for(int i=0;i<8192;i++)header(tileHeaders[i],static_cast<uint8_t*>(tiles)+(i%2048)*32,8,8,false,i/2048);
    for(int p=0;p<2;p++)header(spriteHeaders[p],spriteTexture[p],512,256,true);
    cheatHintTexture=pvr_mem_malloc(256*16*2);
    if(!cheatHintTexture)throw std::runtime_error("Cheats hint texture allocation failed");
    alignas(32) uint16_t hintPixels[256*16]{};
    sor::cheats::drawHint(hintPixels,[](void *context,int x,int y,unsigned r,unsigned g,unsigned b){
        static_cast<uint16_t*>(context)[(y-196)*256+x-32]=sor::VdpScene::rgb1555(r,g,b);
    });
    pvr_txr_load(hintPixels,cheatHintTexture,sizeof(hintPixels));
    header(cheatHintHeader,cheatHintTexture,256,16,true);
    for(size_t i=0;i<sor::TitleCaption::regions.size();i++){
        const auto &r=sor::TitleCaption::regions[i];
        titleTextures[i]=pvr_mem_malloc(r.textureWidth*r.textureHeight*2);
        if(!titleTextures[i])throw std::runtime_error("PowerVR title texture budget exhausted");
        header(titleHeaders[i],titleTextures[i],r.textureWidth,r.textureHeight,true);
    }
    sor_log("PowerVR indexed tile cache: 65536 bytes; sprite layers: 524288 bytes\n");
    load_art();
    sor::cheats::menu.setEnhancedGraphics(SOR_DEFAULT_ENHANCED);
    sor::cheats::menu.setSmoothAnimation(SOR_DEFAULT_SMOOTH);
    if(!replay_active())sor_flush_log();   // show start-up (art loading) at once
}
void dc_renderer_shutdown(){
    if(tiles)pvr_mem_free(tiles);
    for(auto p:spriteTexture)if(p)pvr_mem_free(p);
    if(cheatHintTexture)pvr_mem_free(cheatHintTexture);
    for(auto p:titleTextures)if(p)pvr_mem_free(p);
    for(auto p:artTextures)if(p)pvr_mem_free(p);
    artTextures.clear();artHeaders.clear();art=sor::ArtCatalog();
    scene.reset();
}
bool dc_render_vdp(VDPState &state,VDPRenderer &renderer,const sor::TitleCaption &title){
    const auto begin=timer_us_gettime64();
    const auto &settings=sor::cheats::menu.settings();
    load_selection(gameRound,gameCharacters,settings.enhancedGraphics,settings.smoothAnimation);
    scene->enhanced=settings.enhancedGraphics;scene->smooth=settings.smoothAnimation;scene->art=&art;
    if(!scene->buildCached(state,renderer)||scene->count>maxTileQuads)return false;
    const bool enhanced=scene->enhanced;
    const bool same=scene->reused;
    const auto compiled=timer_us_gettime64();
    pvr_wait_ready();
    const auto ready=timer_us_gettime64();
    if(title.brightness && title.textureKey()!=titleTextureKey){
        for(size_t i=0;i<sor::TitleCaption::regions.size();i++){
            const auto &region=sor::TitleCaption::regions[i];
            alignas(32) uint16_t pixels[sor::TitleCaption::MAX_TEXTURE_PIXELS]{};
            title.drawLayer(i,[&](int x,int y,unsigned r,unsigned g,unsigned b){
                pixels[(y-region.y)*region.textureWidth+x-region.x]=sor::VdpScene::rgb1555(r,g,b);
            });
            pvr_txr_load(pixels,titleTextures[i],region.textureWidth*region.textureHeight*2);
        }
        titleTextureKey=title.textureKey();
    }
    bool opacityChanged=false;
    if(!same || !frames){
        for(int p=0;p<4;p++)if(!frames || !sor::equal_bytes(previousColors+p*16,scene->colors+p*16,32)){
            for(int c=0;c<16;c++)pvr_set_pal_entry(p*16+c,c?scene->colors[p*16+c]:0);
            std::memcpy(previousColors+p*16,scene->colors+p*16,32);
        }
        // Only tiles written since they were last checked can differ.
        for(int t=0;t<2048;t++)if((!frames || state.tileDirty_[t]) && (state.tileDirty_[t]=0,
                                  !sor::equal_bytes(previousTiles+t*32,state.vram_+t*32,32))){
            valid[t]=0;std::memcpy(previousTiles+t*32,state.vram_+t*32,32);
            const bool wasOpaque=opaque[t];
            opaque[t]=false;for(int j=0;j<32;j++)opaque[t]|=state.vram_[t*32+j]!=0;
            opacityChanged|=planeTiles[t] && wasOpaque!=opaque[t];
        }
    }
    alignas(32) uint16_t decoded[16];
    const auto uploadTile=[&](int t){
        if(!opaque[t] || valid[t])return;
        sor::pack_pvr_tile4(state.vram_+t*32,decoded);
        pvr_txr_load(decoded,static_cast<uint8_t*>(tiles)+t*32,32);
        valid[t]=1;
    };
    if(!same || !frames)for(size_t i=0;i<scene->count;i++)uploadTile(scene->quads[i].tile);
    if(enhanced && (!same || !frames))for(size_t i=0;i<scene->spriteTileCount;i++)uploadTile(scene->spriteTiles[i].tile);
    unsigned spriteBytes=0;
    if(!enhanced && (!same || !frames))for(int p=0;p<2;p++){
        // Include the previous extent to erase pixels vacated by moving sprites.
        const int top=frames?std::min(uploadedTop[p],scene->spriteTop[p]):0;
        const int bottom=frames?std::max(uploadedBottom[p],scene->spriteBottom[p]):256;
        if(bottom>top){
            const unsigned bytes=(bottom-top)*1024;
            pvr_txr_load(scene->sprites[p]+top*512,static_cast<uint8_t*>(spriteTexture[p])+top*1024,bytes);
            spriteBytes+=bytes;
        }
        uploadedTop[p]=scene->spriteTop[p];uploadedBottom[p]=scene->spriteBottom[p];
    }
    const auto uploaded=timer_us_gettime64();
    if(!packetsValid || !scene->planesReused || opacityChanged || packetsEnhanced!=enhanced){
        packetCount=0;
        std::fill_n(planeTiles,2048,false);
        float sx=640.f/scene->width,sy=480.f/scene->height;
        for(size_t i=0;i<scene->count;i++){
            const auto &q=scene->quads[i];
            planeTiles[q.tile]=true; // Includes empty tiles that may become visible.
            if(!opaque[q.tile])continue;
            quad(packets[packetCount++],tileHeaders[q.palette*2048+q.tile],q.x*sx,q.y*sy,q.w*sx,q.h*sy,q.depth,q.u0/8.f,q.v0/8.f,q.u1/8.f,q.v1/8.f);
        }
        if(!enhanced)for(int p=0;p<2;p++)quad(packets[packetCount++],spriteHeaders[p],0,0,640,480,p?6:3,0,0,scene->width/512.f,scene->height/256.f);
        packetsValid=true;packetsEnhanced=enhanced;
    }
    if(enhanced && (!same || !frames)){
        // Depth: the sprite's layer (3 low, 6 high priority), then its link
        // order, earlier sprites in front as on the VDP.
        spritePacketCount=0;
        const float sx=640.f/scene->width,sy=480.f/scene->height;
        for(size_t i=0;i<scene->spriteTileCount;i++){
            const auto &t=scene->spriteTiles[i];
            quad(spritePackets[spritePacketCount++],tileHeaders[t.palette*2048+t.tile],t.x*sx,t.y*sy,8*sx,8*sy,
                 (t.layer?6:3)+(79-t.order)*0.01f,t.hflip?1:0,t.vflip?1:0,t.hflip?0:1,t.vflip?0:1);
        }
        for(size_t i=0;i<scene->artCount;i++){
            const auto &d=scene->artDraws[i];const auto &f=art.frames()[d.frame];const auto &page=art.pages()[f.page];
            // Art is at twice the original resolution; its anchor sits on the object's.
            const float ax=d.flip?f.w-f.anchorX:f.anchorX,u0=f.u/float(page.width),u1=(f.u+f.w)/float(page.width);
            // Rows (art pixels) left by a sprite mask: the game blanks what passes behind the HUD.
            const int top=d.y*2-f.anchorY;
            const int v0=std::max(0,d.lineFrom*2-top),v1=std::min<int>(f.h,d.lineTo*2-top);
            if(v1<=v0)continue;
            quad(spritePackets[spritePacketCount++],artHeaders[f.page],(d.x*2-ax)*sx/2,(top+v0)*sy/2,f.w*sx/2,(v1-v0)*sy/2,
                 (d.layer?6:3)+(79-d.order)*0.01f,d.flip?u1:u0,(f.v+v0)/float(page.height),d.flip?u0:u1,(f.v+v1)/float(page.height));
            if(!d.tint.identity()){
                // Fades scale the art's colours; flashes add to them.
                const uint32_t argb=0xff000000u|uint32_t(d.tint.scale[0])<<16|uint32_t(d.tint.scale[1])<<8|d.tint.scale[2];
                const uint32_t oargb=uint32_t(d.tint.offset[0])<<16|uint32_t(d.tint.offset[1])<<8|d.tint.offset[2];
                for(auto &v:spritePackets[spritePacketCount-1].vertices){v.argb=argb;v.oargb=oargb;}
            }
        }
    }
    const auto commands=timer_us_gettime64();
    auto bg=scene->background;pvr_set_bg_color(((bg>>10)&31)/31.f,((bg>>5)&31)/31.f,(bg&31)/31.f);
    pvr_scene_begin();pvr_list_begin(PVR_LIST_PT_POLY);
    pvr_prim(packets,packetCount*sizeof(Packet));
    if(enhanced && spritePacketCount)pvr_prim(spritePackets,spritePacketCount*sizeof(Packet));
    if(sor::cheats::hintVisible()){
        alignas(32) Packet hint;
        const float sx=640.f/scene->width,sy=480.f/scene->height;
        quad(hint,cheatHintHeader,32*sx,196*sy,256*sx,16*sy,7,0,0,1,1);
        pvr_prim(&hint,sizeof(hint));
    }
    if(title.brightness){
        const float sx=640.f/scene->width,sy=480.f/scene->height;
        for(size_t i=0;i<sor::TitleCaption::regions.size();i++){
            const auto &r=sor::TitleCaption::regions[i];
            alignas(32) Packet caption;
            quad(caption,titleHeaders[i],r.x*sx,r.y*sy,r.width*sx,r.height*sy,7,0,0,
                float(r.width)/r.textureWidth,float(r.height)/r.textureHeight);
            pvr_prim(&caption,sizeof(caption));
        }
    }
    pvr_list_finish();pvr_scene_finish();
    const auto finished=timer_us_gettime64();
    // Aggregate every frame: sparse samples mostly hit unchanged VDP frames
    // and conceal the cost of animation, scrolling, and texture replacement.
    static uint64_t sums[5]{},peaks[5]{},spriteSum=0;
    const uint64_t elapsed[]={compiled-begin,ready-compiled,uploaded-ready,commands-uploaded,finished-commands};
    for(int i=0;i<5;i++){sums[i]+=elapsed[i];peaks[i]=std::max(peaks[i],elapsed[i]);}
    spriteSum+=spriteBytes;
    if(++frames%600==0){
        sor_log("GPU_STATS n=600 scene=%llu/%llu wait=%llu/%llu upload=%llu/%llu commands=%llu/%llu submit=%llu/%llu sprite_bytes_mean=%llu (mean/max us)\n",
            sums[0]/600,peaks[0],sums[1]/600,peaks[1],sums[2]/600,peaks[2],sums[3]/600,peaks[3],sums[4]/600,peaks[4],spriteSum/600);
        std::fill_n(sums,5,0);std::fill_n(peaks,5,0);spriteSum=0;
    }
    return true;
}
