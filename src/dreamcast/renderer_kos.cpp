#include "diagnostics.hpp"
#include <kos.h>
#include <cstring>
#include <cmath>
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
uint32_t packetsFogKey=0;   // the haze the tile packets carry: none, or the round and the wall line
// Enhanced graphics: hardware sprite cells and replacement-art frames, one
// quad each, rebuilt when the scene changes (docs/REMASTER.md).
constexpr size_t maxSpritePackets=sor::VdpScene::MAX_SPRITE_TILES+80;
Packet spritePackets[maxSpritePackets];
size_t spritePacketCount=0;
sor::ArtCatalog art;
std::vector<uint8_t> artPackage;
std::vector<pvr_ptr_t> artTextures;
std::vector<pvr_poly_hdr_t> artHeaders;
// Dynamic lighting (src/render/scene_light.hpp): shadows are the art drawn
// again, black and translucent, on the ground; pools of light are added.
std::vector<pvr_poly_hdr_t> shadowHeaders,rimHeaders;
// Lit art: a strip of COLUMNS columns of vertices, each with its own light, so
// that the art is shaded as a rounded form across its width.
struct alignas(32) LitPacket {pvr_poly_hdr_t header;pvr_vertex_t vertices[sor::CornerLight::COLUMNS*2];};
static_assert(sizeof(LitPacket)%32==0);
LitPacket litPackets[80];
size_t litPacketCount=0;
pvr_ptr_t glowTexture=nullptr;
pvr_poly_hdr_t glowHeader,blobHeader,spillHeader;   // light added; matter (contact shadows, smoke); light spilt on the ground
// Weather (src/render/scene_weather.hpp): rain streaks and mist, 64 x 64
// alpha textures that wrap; the rain is added, the mist covers.
pvr_ptr_t weatherTextures[2]{};
pvr_poly_hdr_t streakHeader,noiseHeader;
constexpr size_t maxLightPackets=sor::VdpScene::MAX_GLOWS+2*sor::SceneLight::COLS+80*(1+3+2+1)+sor::Particles::MAX+sor::MAX_WEATHER_QUADS;   // glows, spill, per object: contact, shadows, rims, reflection; particles; weather
Packet lightPackets[maxLightPackets];
size_t lightPacketCount=0,staticLightPackets=0;
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
// The art of a page again, in the translucent list: only its alpha is used.
void shadow_header(pvr_poly_hdr_t &h,pvr_ptr_t texture,int w,int hgt,int bank){
    pvr_poly_cxt_t c;
    pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_PAL8BPP|PVR_TXRFMT_8BPP_PAL(bank),w,hgt,texture,PVR_FILTER_BILINEAR);
    c.txr.env=PVR_TXRENV_MODULATEALPHA;      // colour and alpha: texture * vertex
    c.gen.culling=PVR_CULLING_NONE;
    c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
    pvr_poly_compile(&h,&c);
}
// The art of a page as a flat colour (the offset colour; the vertex colour is
// black), translucent: the rim light.
void rim_header(pvr_poly_hdr_t &h,pvr_ptr_t texture,int w,int hgt,int bank){
    pvr_poly_cxt_t c;
    pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_PAL8BPP|PVR_TXRFMT_8BPP_PAL(bank),w,hgt,texture,PVR_FILTER_NONE);
    c.txr.env=PVR_TXRENV_MODULATEALPHA;c.gen.specular=PVR_SPECULAR_ENABLE;
    c.gen.culling=PVR_CULLING_NONE;
    c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
    pvr_poly_compile(&h,&c);
}
uint32_t argb_of(const uint8_t *rgb,unsigned alpha=255){return uint32_t(alpha)<<24|uint32_t(rgb[0])<<16|uint32_t(rgb[1])<<8|rgb[2];}
void quad(Packet &packet,const pvr_poly_hdr_t &h,float x,float y,float w,float hgt,float z,float u0,float v0,float u1,float v1){
    // Every word of the packet is written here: no memset and no memcpy per
    // quad (thousands of them whenever the planes scroll).
    static_assert(sizeof(pvr_poly_hdr_t)==32);
    const uint32_t *from=reinterpret_cast<const uint32_t*>(&h);uint32_t *to=reinterpret_cast<uint32_t*>(&packet.header);
    for(int i=0;i<8;i++)to[i]=from[i];
    const float xx[]={x,x+w,x,x+w},yy[]={y,y,y+hgt,y+hgt};
    for(int i=0;i<4;i++){auto &v=packet.vertices[i];v.flags=i==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v.x=xx[i];v.y=yy[i];v.z=z;v.u=(i&1)?u1:u0;v.v=(i&2)?v1:v0;v.argb=0xffffffff;v.oargb=0;}
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
    artTextures.assign(art.pages().size(),nullptr);artHeaders.resize(art.pages().size());shadowHeaders.resize(art.pages().size());rimHeaders.resize(art.pages().size());
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
        shadow_header(shadowHeaders[i],t,page.width,page.height,1+int(page.palette));
        rim_header(rimHeaders[i],t,page.width,page.height,1+int(page.palette));
    }
    sor_log("Enhanced art: round %u characters %x: %zu pages loaded, %zu did not fit, %zu PowerVR bytes in use; free %lu; %llu ms\n",round,characters,loaded,missing,bytes,
            (unsigned long)pvr_mem_available(),(unsigned long long)((timer_us_gettime64()-start)/1000));
    if(!replay_active())sor_flush_log();
}
unsigned gameRound=1,gameCharacters=0;bool gamePlaying=false;
}
// Followed outside play too: the round intro is when a round's art should load.
// `playing`: in a round (not the title, menus, cutscenes or the ending): the weather's.
void dc_renderer_game_state(unsigned round,unsigned characters,bool playing){gameRound=round;gameCharacters=characters;gamePlaying=playing;}
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
    {
        // A pool of light: white, fading from the middle as the host preview's does.
        glowTexture=pvr_mem_malloc(32*32*2);
        if(!glowTexture)throw std::runtime_error("PowerVR glow texture allocation failed");
        alignas(32) static uint16_t pixels[32*32];
        for(int y=0;y<32;y++)for(int x=0;x<32;x++){
            const float dx=(x-15.5f)/15.5f,dy=(y-15.5f)/15.5f,d=std::sqrt(dx*dx+dy*dy);
            const int alpha=d>=1?0:int((1-d)*(1-d)*15+.5f);
            pixels[y*32+x]=uint16_t(alpha<<12|0xFFF);
        }
        pvr_txr_load_ex(pixels,glowTexture,32,32,PVR_TXRLOAD_16BPP);
        pvr_poly_cxt_t c;
        pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444,32,32,glowTexture,PVR_FILTER_BILINEAR);
        c.txr.env=PVR_TXRENV_MODULATEALPHA;
        c.blend.src=PVR_BLEND_SRCALPHA;c.blend.dst=PVR_BLEND_ONE;   // added to the picture
        c.gen.culling=PVR_CULLING_NONE;
        c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
        pvr_poly_compile(&glowHeader,&c);
        c.blend.dst=PVR_BLEND_INVSRCALPHA;
        pvr_poly_compile(&blobHeader,&c);
        pvr_poly_cxt_col(&c,PVR_LIST_TR_POLY);
        c.blend.src=PVR_BLEND_SRCALPHA;c.blend.dst=PVR_BLEND_ONE;
        c.gen.culling=PVR_CULLING_NONE;
        c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
        pvr_poly_compile(&spillHeader,&c);
    }
    for(int t=0;t<2;t++){
        // Weather: rain streaks (added) and mist (covering), white with the texture's alpha.
        constexpr int size=sor::WEATHER_TEXTURE;
        weatherTextures[t]=pvr_mem_malloc(size*size*2);
        if(!weatherTextures[t])throw std::runtime_error("PowerVR weather texture allocation failed");
        alignas(32) static uint16_t pixels[size*size];
        const uint8_t *alpha=sor::weather_texture(t?sor::WeatherTexture::NOISE:sor::WeatherTexture::STREAK);
        for(int i=0;i<size*size;i++)pixels[i]=uint16_t((alpha[i]>>4)<<12|0xFFF);
        pvr_txr_load_ex(pixels,weatherTextures[t],size,size,PVR_TXRLOAD_16BPP);
        pvr_poly_cxt_t c;
        pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444,size,size,weatherTextures[t],PVR_FILTER_BILINEAR);
        c.txr.env=PVR_TXRENV_MODULATEALPHA;
        c.blend.src=PVR_BLEND_SRCALPHA;c.blend.dst=t?PVR_BLEND_INVSRCALPHA:PVR_BLEND_ONE;
        c.gen.culling=PVR_CULLING_NONE;
        c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
        pvr_poly_compile(t?&noiseHeader:&streakHeader,&c);
    }
    sor_log("PowerVR indexed tile cache: 65536 bytes; sprite layers: 524288 bytes\n");
    load_art();
    sor::cheats::menu.setEnhancedGraphics(SOR_DEFAULT_ENHANCED);
    sor::cheats::menu.setSmoothAnimation(SOR_DEFAULT_SMOOTH);
    sor::cheats::menu.setDynamicLighting(SOR_DEFAULT_LIGHTING);
    sor::cheats::menu.setWeather(SOR_DEFAULT_WEATHER);
    if(!replay_active())sor_flush_log();   // show start-up (art loading) at once
}
void dc_renderer_shutdown(){
    if(tiles)pvr_mem_free(tiles);
    for(auto p:spriteTexture)if(p)pvr_mem_free(p);
    if(cheatHintTexture)pvr_mem_free(cheatHintTexture);
    if(glowTexture)pvr_mem_free(glowTexture);
    for(auto p:weatherTextures)if(p)pvr_mem_free(p);
    for(auto p:titleTextures)if(p)pvr_mem_free(p);
    for(auto p:artTextures)if(p)pvr_mem_free(p);
    artTextures.clear();artHeaders.clear();shadowHeaders.clear();rimHeaders.clear();art=sor::ArtCatalog();
    scene.reset();
}
bool dc_render_vdp(VDPState &state,VDPRenderer &renderer,const sor::TitleCaption &title){
    const auto begin=timer_us_gettime64();
    const auto &settings=sor::cheats::menu.settings();
    load_selection(gameRound,gameCharacters,settings.enhancedGraphics,settings.smoothAnimation);
    scene->enhanced=settings.enhancedGraphics;scene->smooth=settings.smoothAnimation;scene->art=&art;
    // Light and weather only in a round: the title, menus and cutscenes are drawn as they are.
    scene->lighting=settings.enhancedGraphics&&settings.dynamicLighting&&gamePlaying;scene->round=gameRound;
    scene->weather=scene->lighting&&settings.weather&&gamePlaying;
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
    // Weather: haze. The backdrop's tiles take the fog's colour by their height
    // (vertex colour and offset colour), the far plane more: no extra quads.
    const uint32_t fogKey=scene->weatherOn()&&scene->weatherProfile().fog?0x80000000u|uint32_t(scene->round)<<16|uint32_t(scene->sceneLight().horizon()&0xFFFF):0;
    if(!packetsValid || !scene->planesReused || opacityChanged || packetsEnhanced!=enhanced || packetsFogKey!=fogKey){
        packetCount=0;
        std::fill_n(planeTiles,2048,false);
        float sx=640.f/scene->width,sy=480.f/scene->height;
        // The haze per line, once, as the two vertex words it needs: thousands of
        // quads share a few hundred lines, and most lines (the ground) have none.
        static uint32_t fogKeep[2][257],fogAdd[2][257];
        if(fogKey){
            const uint8_t *fog=scene->weatherProfile().fogColour;
            for(int y=0;y<=scene->height&&y<=256;y++)for(int far=0;far<2;far++){
                const int f=scene->fogAt(y,far);
                const uint8_t keep[3]={uint8_t(255-f),uint8_t(255-f),uint8_t(255-f)},add[3]={uint8_t(fog[0]*f/255),uint8_t(fog[1]*f/255),uint8_t(fog[2]*f/255)};
                fogKeep[far][y]=f?argb_of(keep):0;fogAdd[far][y]=argb_of(add,0);
            }
        }
        for(size_t i=0;i<scene->count;i++){
            const auto &q=scene->quads[i];
            planeTiles[q.tile]=true; // Includes empty tiles that may become visible.
            if(!opaque[q.tile])continue;
            Packet &p=packets[packetCount++];
            quad(p,tileHeaders[q.palette*2048+q.tile],q.x*sx,q.y*sy,q.w*sx,q.h*sy,q.depth,q.u0/8.f,q.v0/8.f,q.u1/8.f,q.v1/8.f);
            if(fogKey&&i<scene->planeEnd[1]){
                const int far=i<scene->planeEnd[0];
                const uint32_t k0=fogKeep[far][q.y],k1=fogKeep[far][q.y+q.h];
                if(!k0&&!k1)continue;
                p.header.cmd|=PVR_TA_CMD_SPECULAR;
                const uint32_t a0=fogAdd[far][q.y],a1=fogAdd[far][q.y+q.h];
                p.vertices[0].argb=k0?k0:0xffffffff;p.vertices[1].argb=p.vertices[0].argb;p.vertices[0].oargb=a0;p.vertices[1].oargb=a0;
                p.vertices[2].argb=k1?k1:0xffffffff;p.vertices[3].argb=p.vertices[2].argb;p.vertices[2].oargb=a1;p.vertices[3].oargb=a1;
            }
        }
        if(!enhanced)for(int p=0;p<2;p++)quad(packets[packetCount++],spriteHeaders[p],0,0,640,480,p?6:3,0,0,scene->width/512.f,scene->height/256.f);
        packetsValid=true;packetsEnhanced=enhanced;packetsFogKey=fogKey;
    }
    if(enhanced && (!same || !frames)){
        // Depth: the sprite's layer (3 low, 6 high priority), then its link
        // order, earlier sprites in front as on the VDP.
        spritePacketCount=0;lightPacketCount=0;litPacketCount=0;
        const float sx=640.f/scene->width,sy=480.f/scene->height;
        for(size_t i=0;i<scene->glowCount;i++){
            const auto &g=scene->glows[i];
            quad(lightPackets[lightPacketCount++],glowHeader,(g.x-g.radiusX)*sx,(g.y-g.radiusY)*sy,g.radiusX*2*sx,g.radiusY*2*sy,2.8f,0,0,1,1);
            for(auto &v:lightPackets[lightPacketCount-1].vertices)v.argb=argb_of(g.colour,g.strength);
        }
        if(scene->lighting){
            // Light spilt on the ground by the backdrop's lights: per column, rising
            // to full at the horizon line, then fading (raster_enhanced does the same).
            const auto &light=scene->sceneLight();
            const float top=light.horizon()-sor::SceneLight::SPILL_RISE,middle=light.horizon(),bottom=light.horizon()+sor::SceneLight::SPILL_DEPTH;
            for(int column=0;column<sor::SceneLight::COLS;column++){
                const auto &a=light.spill(column),&b=light.spill(column+1);
                if(!a.strength&&!b.strength)continue;
                const float x=column*sor::SceneLight::CELL*sx,w=sor::SceneLight::CELL*sx;
                for(int part=0;part<2;part++){
                    Packet &p=lightPackets[lightPacketCount++];
                    quad(p,spillHeader,x,(part?middle:top)*sy,w,((part?bottom:middle)-(part?middle:top))*sy,2.8f,0,0,0,0);
                    for(int k=0;k<4;k++){
                        const auto &from=(k&1)?b:a;
                        const bool full=part?!(k&2):(k&2);        // the horizon's vertices
                        p.vertices[k].argb=argb_of(from.colour,full?from.strength:0);
                    }
                }
            }
        }
        for(size_t i=0;i<scene->spriteTileCount;i++){
            const auto &t=scene->spriteTiles[i];
            quad(spritePackets[spritePacketCount++],tileHeaders[t.palette*2048+t.tile],t.x*sx,t.y*sy,8*sx,8*sy,
                 (t.layer?6:3)+(79-t.order)*0.01f,t.hflip?1:0,t.vflip?1:0,t.hflip?0:1,t.vflip?0:1);
            if(t.shade[0]!=255||t.shade[1]!=255||t.shade[2]!=255||t.glow[0]||t.glow[1]||t.glow[2]){
                // A lit object drawn from its pieces: one light for them all. The added
                // light needs the offset colour, which the shared tile headers do not enable.
                Packet &p=spritePackets[spritePacketCount-1];
                p.header.cmd|=PVR_TA_CMD_SPECULAR;
                for(auto &v:p.vertices){v.argb=argb_of(t.shade);v.oargb=argb_of(t.glow,0);}
            }
        }
        for(size_t i=0;i<scene->artCount;i++){
            const auto &d=scene->artDraws[i];const auto &f=art.frames()[d.frame];const auto &page=art.pages()[f.page];
            // Art is at twice the original resolution; its anchor sits on the object's.
            const float ax=d.flip?f.w-f.anchorX:f.anchorX,u0=f.u/float(page.width),u1=(f.u+f.w)/float(page.width);
            // Rows (art pixels) left by a sprite mask: the game blanks what passes behind the HUD.
            const int top=d.y*2-f.anchorY;
            const int v0=std::max(0,d.lineFrom*2-top),v1=std::min<int>(f.h,d.lineTo*2-top);
            if(v1<=v0)continue;
            const float depth=(d.layer?6:3)+(79-d.order)*0.01f;
            const float left=(d.x*2-ax)*sx/2,y0=(top+v0)*sy/2,y1=(top+v1)*sy/2,tv0=(f.v+v0)/float(page.height),tv1=(f.v+v1)/float(page.height);
            if(d.lit){
                // Lighting: a tint per column of vertices, top and bottom (the game's fade
                // or flash included), interpolated between them.
                LitPacket &lit=litPackets[litPacketCount++];
                lit={};lit.header=artHeaders[f.page];
                constexpr int columns=sor::CornerLight::COLUMNS;
                for(int c=0;c<columns;c++)for(int row=0;row<2;row++){
                    pvr_vertex_t &v=lit.vertices[c*2+row];
                    const float t=c/float(columns-1);
                    v.flags=c==columns-1&&row?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
                    v.x=left+t*f.w*sx/2;v.y=row?y1:y0;v.z=depth;
                    v.u=d.flip?u1+(u0-u1)*t:u0+(u1-u0)*t;v.v=row?tv1:tv0;
                    v.argb=argb_of(d.light.column[c][row].scale);v.oargb=argb_of(d.light.column[c][row].offset,0);
                }
                // Rim light: the art again as a flat colour, moved towards the lights, just behind the art.
                for(int g=0;g<2;g++){
                    const auto &rim=d.light.rim[g];
                    if(!rim.alpha)continue;
                    const float shift=(g?sor::VdpScene::RIM_SHIFT:-sor::VdpScene::RIM_SHIFT)*sx/2;
                    quad(lightPackets[lightPacketCount++],rimHeaders[f.page],left+shift,y0-sy/2,f.w*sx/2,y1-y0,depth-0.004f,d.flip?u1:u0,tv0,d.flip?u0:u1,tv1);
                    for(auto &v:lightPackets[lightPacketCount-1].vertices){v.argb=uint32_t(rim.alpha)<<24;v.oargb=argb_of(rim.colour,0);}
                }
                if(d.shadow){
                    // Contact shadow: a dark ellipse under the feet, smaller the higher the object is.
                    const float rx=std::max(6,f.w*sor::VdpScene::contactScale(d.ground-d.y)/256/2),ry=std::max(3.f,float(int(rx)/4)),cx=d.x*2-ax+f.w/2;
                    quad(lightPackets[lightPacketCount++],blobHeader,(cx-rx)*sx/2,(d.ground*2-ry)*sy/2,rx*sx,ry*sy,2.85f,0,0,1,1);
                    for(auto &v:lightPackets[lightPacketCount-1].vertices)v.argb=uint32_t(sor::VdpScene::CONTACT_ALPHA)<<24;
                    // Cast shadows: the whole frame, black, from the ground line towards the
                    // viewer: a row `above` the feet (art pixels) lies above*length/64 below
                    // the line, shifted by above*lean/64.
                    for(const auto &shadow:d.light.shadow){
                        if(!shadow.alpha)continue;
                        Packet &p=lightPackets[lightPacketCount++];
                        quad(p,shadowHeaders[f.page],0,0,0,0,2.9f,d.flip?u1:u0,f.v/float(page.height),d.flip?u0:u1,(f.v+f.h)/float(page.height));
                        for(int k=0;k<4;k++){
                            const float above=(k&2)?f.anchorY-f.h:f.anchorY,x=d.x*2-ax+((k&1)?f.w:0)+above*shadow.lean/64;
                            p.vertices[k].x=x*sx/2;p.vertices[k].y=(d.ground*2+above*shadow.length/64)*sy/2;
                            p.vertices[k].argb=uint32_t(shadow.alpha)<<24;
                        }
                    }
                    // Wet ground: the art again, mirrored in the ground line (an object in
                    // the air reflects as far below it as it is above), dimmed, fading away
                    // from the feet and with height.
                    if(const int reflect=sor::VdpScene::reflectAlpha(scene->reflectAlpha(),d.ground-d.y)){
                        Packet &p=lightPackets[lightPacketCount++];
                        quad(p,shadowHeaders[f.page],left,(2*d.ground-d.y)*sy,f.w*sx/2,f.anchorY*sor::REFLECT_LENGTH/64*sy/2,2.92f,
                             d.flip?u1:u0,(f.v+f.anchorY)/float(page.height),d.flip?u0:u1,f.v/float(page.height));
                        for(int k=0;k<4;k++)p.vertices[k].argb=argb_of(sor::REFLECT_TINT,(k&2)?0:reflect);
                    }
                }
                continue;
            }
            quad(spritePackets[spritePacketCount++],artHeaders[f.page],left,y0,f.w*sx/2,y1-y0,depth,d.flip?u1:u0,tv0,d.flip?u0:u1,tv1);
            if(!d.tint.identity()){
                // Fades scale the art's colours; flashes add to them.
                const uint32_t argb=0xff000000u|uint32_t(d.tint.scale[0])<<16|uint32_t(d.tint.scale[1])<<8|d.tint.scale[2];
                const uint32_t oargb=uint32_t(d.tint.offset[0])<<16|uint32_t(d.tint.offset[1])<<8|d.tint.offset[2];
                for(auto &v:spritePackets[spritePacketCount-1].vertices){v.argb=argb;v.oargb=oargb;}
            }
        }
    }
    if(enhanced){
        // Particles, over everything: light is added, smoke covers. They move every
        // frame, the rest of the light's quads only when the scene changes.
        if(!same || !frames)staticLightPackets=lightPacketCount;
        lightPacketCount=staticLightPackets;
        const float sx=640.f/scene->width,sy=480.f/scene->height;
        for(size_t i=0;i<scene->particleCount;i++){
            const auto &p=scene->particleDraws[i];
            quad(lightPackets[lightPacketCount++],p.additive?glowHeader:blobHeader,(p.x-p.radius)*sx/2,(p.y-p.radius)*sy/2,p.radius*sx,p.radius*sy,6.95f,0,0,1,1);
            for(auto &v:lightPackets[lightPacketCount-1].vertices)v.argb=argb_of(p.colour,p.alpha);
        }
        // The weather's sheets of rain and mist, the lights' smears and shafts
        // and the flash of lightning: between the planes, over the ground under
        // the sprites, or over everything (under the particles).
        for(size_t i=0;i<scene->weatherQuadCount;i++){
            const auto &q=scene->weatherQuads[i];
            Packet &p=lightPackets[lightPacketCount++];
            const float depth=q.depth==sor::WeatherQuad::BEHIND?1.5f:q.depth==sor::WeatherQuad::GROUND?2.95f:6.9f;
            quad(p,q.texture==sor::WeatherTexture::STREAK?streakHeader:q.texture==sor::WeatherTexture::NOISE?noiseHeader:spillHeader,0,0,0,0,depth,0,0,0,0);
            for(int k=0;k<4;k++){
                auto &v=p.vertices[k];
                v.x=q.x[k]*sx/2;v.y=q.y[k]*sy/2;
                v.u=q.u[k]/float(sor::WEATHER_TEXTURE);v.v=q.v[k]/float(sor::WEATHER_TEXTURE);
                v.argb=argb_of(q.colour,q.alpha[k]);
            }
        }
    }
    const auto commands=timer_us_gettime64();
    auto bg=scene->background;pvr_set_bg_color(((bg>>10)&31)/31.f,((bg>>5)&31)/31.f,(bg&31)/31.f);
    pvr_scene_begin();pvr_list_begin(PVR_LIST_PT_POLY);
    pvr_prim(packets,packetCount*sizeof(Packet));
    if(enhanced && spritePacketCount)pvr_prim(spritePackets,spritePacketCount*sizeof(Packet));
    if(enhanced && litPacketCount)pvr_prim(litPackets,litPacketCount*sizeof(LitPacket));
    const bool lights=enhanced&&scene->lighting&&lightPacketCount;
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
    pvr_list_finish();
    if(lights){
        pvr_list_begin(PVR_LIST_TR_POLY);
        pvr_prim(lightPackets,lightPacketCount*sizeof(Packet));
        pvr_list_finish();
    }
    pvr_scene_finish();
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
