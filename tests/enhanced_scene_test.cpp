// Enhanced rendering: an object recorded by the sprite probe and present in the
// art catalog (under its line's colours, on a page of the current round) is
// drawn once with its art in its SAT slot, its hardware pieces
// are left out, other sprites stay as cells, and a probe build that no longer
// matches VRAM's sprite table falls back to the original pieces.
#include "vdp_scene.hpp"
#include "sprite_probe.hpp"
#include "art_catalog.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <zlib.h>
#include <memory>
#include <vector>
namespace {
void put16(std::vector<uint8_t> &v,unsigned x){v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));}
void put32(std::vector<uint8_t> &v,uint32_t x){put16(v,x&0xFFFF);put16(v,x>>16);}
// SAT record: y, size (cells w/h), link, attr, x (all biased by 128).
void record(VDPState &s,uint8_t *ram,int index,int y,int size,int link,uint16_t attr,int x){
    const uint8_t r[8]={uint8_t(y>>8),uint8_t(y),uint8_t(size),uint8_t(link),uint8_t(attr>>8),uint8_t(attr),uint8_t(x>>8),uint8_t(x)};
    std::memcpy(s.vram_+s.satBase()+index*8,r,8);std::memcpy(ram+0xDA00+index*8,r,8);
    std::memcpy(s.sat_+index*8,r,4);
}
}
int main(){
    VDPState state;state.reset();
    state.regs_[1]=0x40;state.regs_[2]=0x30;state.regs_[4]=5;state.regs_[5]=0x78;state.regs_[12]=0x81;state.regs_[16]=0x11;
    for(int i=0;i<16;i++)state.cram_[i]=uint16_t(i*0x111&0xEEE);
    for(int i=0;i<32;i++)state.vram_[32+i]=0x11;           // tile 1: colour 1 everywhere
    std::vector<uint8_t> ram(65536,0);
    // Record 0: the object's piece (2x2 cells) at screen 40,50; record 1: an
    // unowned 1x1 sprite at 100,60, high priority; record 1 ends the list.
    record(state,ram.data(),0,128+50,0x05,1,0x0001,128+40);
    record(state,ram.data(),1,128+60,0x00,0,0x8001,128+100);
    auto &probe=sor::sprite_probe();
    probe.beginBuild();
    probe.beginObject(0xB800,1,0x054206,false,128+48,128+66,0,0xFFDA00);
    probe.endObject(0xFFDA08,ram.data());
    // One art frame for mapping $054206 in line 0's colours: 4x4 art pixels,
    // anchor 2,4, on a page of rounds 1 and 3 for character 2, palette 1.
    // The art uses colours 1 and 2 of its line.
    const uint16_t mask=0b110,key=sor::colour_key(state.cram_,mask);
    const auto package=[&](uint16_t colours,uint16_t rounds,uint8_t character){
        std::vector<uint8_t> page(64);for(int i=0;i<64;i++)page[i]=i<8*4&&i%8<4?1:0;   // top-left 4x4
        std::vector<uint8_t> z(compressBound(64));uLongf zn=z.size();assert(compress(z.data(),&zn,page.data(),64)==Z_OK);
        std::vector<uint8_t> pak(std::begin("SORART04"),std::end("SORART04")-1);
        put32(pak,1);put32(pak,1);put32(pak,2);
        for(int palette=0;palette<2;palette++)for(int i=0;i<256;i++)put16(pak,palette==1&&i==1?0xFC00:0);   // red
        put16(pak,8);put16(pak,8);put16(pak,rounds);pak.push_back(1);pak.push_back(character);put32(pak,uint32_t(zn));pak.insert(pak.end(),z.begin(),z.begin()+zn);
        put32(pak,0x054206);put16(pak,colours);put16(pak,mask);put16(pak,0);put16(pak,0);put16(pak,0);put16(pak,4);put16(pak,4);put16(pak,2);put16(pak,4);
        return pak;
    };
    const auto pak=package(key,0b101,2);
    sor::ArtCatalog art;assert(art.load(pak.data(),pak.size()));
    assert(art.find(0x054206,state.cram_)&&!art.find(0x054207,state.cram_));
    // Only the current round's pages are found.
    // Only pages of the current round and of a character in play are found;
    // nor is a page the loader could not fit.
    art.select(2,1<<2);assert(!art.find(0x054206,state.cram_));
    art.select(3,1<<1);assert(!art.find(0x054206,state.cram_));
    art.select(3,1<<2);assert(art.find(0x054206,state.cram_));
    art.setUnloaded(0);assert(!art.find(0x054206,state.cram_));
    art.select(0,0);assert(art.find(0x054206,state.cram_));
    // Pages can be left packed and inflated on request (the Dreamcast's loader).
    sor::ArtCatalog lazy;assert(lazy.load(pak.data(),pak.size(),false)&&!lazy.pages()[0].indices);
    std::vector<uint8_t> inflated(64);assert(lazy.inflate(lazy.pages()[0],inflated.data())&&inflated[0]==1&&inflated[4]==0);

    auto scene=std::make_unique<sor::VdpScene>();
    VDPTile tile(state);Framebuffer fb;VDPRenderer renderer(state,tile,fb);
    scene->enhanced=true;scene->art=&art;
    assert(scene->build(state,renderer));
    assert(scene->artCount==1);
    const auto &d=scene->artDraws[0];
    assert(d.x==48&&d.y==66&&d.order==0&&d.layer==0&&!d.flip);
    assert(scene->spriteTileCount==1);                     // the unowned sprite only
    const auto &t=scene->spriteTiles[0];
    assert(t.x==100&&t.y==60&&t.tile==1&&t.layer==1&&t.order==1);
    std::vector<uint16_t> out(640*448);
    sor::raster_enhanced(*scene,state,out.data(),640);
    assert(out[(66*2-4)*640+48*2-2]==0xFC00);              // art's top-left at anchor - (2,4)
    assert(out[(66*2-4)*640+48*2+2]!=0xFC00);              // art is 4 pixels wide
    assert(out[(60*2)*640+100*2]==scene->colors[1]);       // unowned sprite cell

    // Other colours in the line (a flash, a fade, a recoloured enemy): the
    // art is not for them, and the object's pieces are drawn.
    state.cram_[2]^=0x0E0;
    assert(scene->build(state,renderer)&&scene->artCount==0&&scene->spriteTileCount==5);
    state.cram_[2]^=0x0E0;
    assert(scene->build(state,renderer)&&scene->artCount==1);
    // A colour of the line the art does not use (stage colour cycling) changes nothing.
    state.cram_[9]^=0x0E0;
    assert(scene->build(state,renderer)&&scene->artCount==1);

    // Fades and flashes (SORART06: each frame carries its look's CRAM line).
    // The game subtracts a step per channel, clamped at black, or adds one,
    // clamped at white: such a line finds the art with a tint. Other changes
    // find nothing.
    {
        auto pak6=package(key,0xFF,0);pak6[7]='6';
        put32(pak6,0);for(int i=0;i<16;i++)put16(pak6,state.cram_[i]);     // from, line
        sor::ArtCatalog fading;assert(fading.load(pak6.data(),pak6.size()));
        scene->art=&fading;
        uint16_t saved[16];std::memcpy(saved,state.cram_,sizeof saved);
        const auto step=[&](int red,int green){
            for(int i=1;i<16;i++){
                const int r=saved[i]>>1&7,g=saved[i]>>5&7,b=saved[i]>>9&7;
                const auto move=[](int v,int k){return std::max(0,std::min(7,v+k));};
                state.cram_[i]=uint16_t(move(r,red)<<1|move(g,green)<<5|b<<9);
            }
        };
        sor::ArtTint tint;
        assert(fading.find(0x054206,state.cram_,&tint)&&tint.identity());
        step(-1,-2);                                        // fading out: red one step, green two
        assert(!fading.find(0x054206,state.cram_));         // exact colours only, without a tint
        assert(fading.find(0x054206,state.cram_,&tint)&&tint.scale[0]<255&&tint.scale[1]<=tint.scale[0]&&tint.scale[2]==255&&!tint.offset[0]);
        assert(scene->build(state,renderer)&&scene->artCount==1&&!scene->artDraws[0].tint.identity());
        std::vector<uint16_t> faded(640*448);
        sor::raster_enhanced(*scene,state,faded.data(),640);
        const uint16_t was=out[(66*2-4)*640+48*2-2],is=faded[(66*2-4)*640+48*2-2];
        assert((is>>10&31)<(was>>10&31)&&(is&0x8000));      // the red art is darker
        step(2,0);                                          // a flash: red two steps up
        assert(fading.find(0x054206,state.cram_,&tint)&&tint.offset[0]==2*255/7&&!tint.offset[1]&&tint.scale[0]==255);
        state.cram_[1]=uint16_t(saved[1]^0x0E0);            // one colour changed alone: not a fade
        for(int i=2;i<16;i++)state.cram_[i]=saved[i];
        assert(!fading.find(0x054206,state.cram_,&tint));
        std::memcpy(state.cram_,saved,sizeof saved);
        scene->art=&art;
    }

    // Sprite masking: after a sprite with another x, a sprite at x = 0 blanks
    // later sprites on its lines (the game hides what passes behind the HUD).
    // Art starts below the blanked lines; a blanked cell is left out.
    {
        uint8_t saved[24];std::memcpy(saved,state.vram_+state.satBase(),24);
        record(state,ram.data(),0,128+57,0x00,1,0x0001,128+200);   // a HUD sprite on lines 57-64
        record(state,ram.data(),1,128+57,0x00,2,0x0001,0);         // the mask, lines 57-64
        record(state,ram.data(),2,128+50,0x05,3,0x0001,128+40);    // the object's piece, lines 50-65
        record(state,ram.data(),3,128+58,0x00,0,0x0001,128+100);   // an unowned sprite on blanked lines
        probe.beginBuild();
        probe.beginObject(0xB800,1,0x054206,false,128+48,128+66,0,0xFFDA10);
        probe.endObject(0xFFDA18,ram.data());
        scene->art=&art;
        assert(scene->build(state,renderer)&&scene->artCount==1);
        assert(scene->artDraws[0].lineFrom==65&&scene->artDraws[0].lineTo==32767);   // art spans lines 64-65
        assert(scene->spriteTileCount==1&&scene->spriteTiles[0].x==200);            // the HUD sprite only
        for(int i=0;i<4;i++)record(state,ram.data(),i,0,0,0,0,0);
        std::memcpy(state.vram_+state.satBase(),saved,24);std::memcpy(ram.data()+0xDA00,saved,24);
        for(int i=0;i<3;i++)std::memcpy(state.sat_+i*8,saved+i*8,4);
        probe.beginBuild();
        probe.beginObject(0xB800,1,0x054206,false,128+48,128+66,0,0xFFDA00);
        probe.endObject(0xFFDA08,ram.data());
    }

    // Smooth animation: a second pose ($054300) and an in-between from the
    // first to it, on a page of its own (character bit 7), green.
    {
        std::vector<uint8_t> page(64,1);
        std::vector<uint8_t> z(compressBound(64));uLongf zn=z.size();assert(compress(z.data(),&zn,page.data(),64)==Z_OK);
        std::vector<uint8_t> pak(std::begin("SORART05"),std::end("SORART05")-1);
        put32(pak,2);put32(pak,3);put32(pak,2);
        for(int palette=0;palette<2;palette++)for(int i=0;i<256;i++)put16(pak,palette==1&&i==1?0xFC00:0);
        for(uint8_t character:{uint8_t(0),uint8_t(0x80)}){
            put16(pak,8);put16(pak,8);put16(pak,0xFF);pak.push_back(1);pak.push_back(character);put32(pak,uint32_t(zn));pak.insert(pak.end(),z.begin(),z.begin()+zn);
        }
        const auto frame=[&](uint32_t mapping,uint16_t page,uint16_t u,uint32_t from){
            put32(pak,mapping);put16(pak,key);put16(pak,mask);put16(pak,page);put16(pak,u);put16(pak,0);put16(pak,4);put16(pak,4);put16(pak,2);put16(pak,4);put32(pak,from);
        };
        frame(0x054300,1,0,0x054206);frame(0x054206,0,0,0);frame(0x054300,0,4,0);
        sor::ArtCatalog poses;assert(poses.load(pak.data(),pak.size())&&poses.hasInbetweens());
        const sor::ArtFrame *second=poses.find(0x054300,state.cram_),*between=poses.between(0x054206,0x054300,state.cram_);
        assert(second&&between&&second!=between&&between->page==1&&!poses.between(0x054300,0x054206,state.cram_));
        // A new pose brings new tiles: VRAM changes with it, as in the game.
        uint32_t last=0;
        const auto drawn=[&](uint32_t mapping){
            if(mapping!=last)state.vramGeneration_++;
            last=mapping;
            probe.beginBuild();
            probe.beginObject(0xB800,1,mapping,false,128+48,128+66,0,0xFFDA00);
            probe.endObject(0xFFDA08,ram.data());
            assert(scene->buildCached(state,renderer)&&scene->artCount==1);
            return &poses.frames()[scene->artDraws[0].frame];
        };
        scene->art=&poses;scene->invalidate();
        // Off: the new pose at once, and an unchanged frame is reused.
        drawn(0x054206);assert(drawn(0x054300)==second);
        drawn(0x054300);assert(scene->reused);
        // On: the in-between for INBETWEEN_TICKS builds (no reuse meanwhile), then the pose.
        scene->smooth=true;
        drawn(0x054206);
        for(unsigned i=0;i<sor::VdpScene::INBETWEEN_TICKS;i++){assert(drawn(0x054300)==between);assert(!scene->reused);}
        assert(drawn(0x054300)==second);
        drawn(0x054300);assert(scene->reused);
        // No in-between for the way back; nor when its page is not selected.
        assert(drawn(0x054206)->mapping==0x054206);
        poses.select(0,0,false);scene->invalidate();
        assert(drawn(0x054300)==second);
        scene->smooth=false;scene->art=&art;scene->invalidate();
    }

    // Dynamic lighting: an object of the playfield is tinted per corner by the
    // backdrop beside it (above the horizon), brighter towards the light, and
    // casts a shadow on its ground line, leaning away from the light. An object
    // in the air keeps its shadow on the ground. Off: nothing changes.
    {
        const auto place=[&](int16_t level){
            probe.beginBuild();
            probe.beginObject(0xB800,1,0x054206,false,128+48,128+66,0,0xFFDA00,0,level);
            probe.endObject(0xFFDA08,ram.data());
        };
        place(160);
        assert(scene->build(state,renderer)&&scene->artCount==1&&!scene->artDraws[0].lit&&!scene->artDraws[0].shadow);
        // A white block of plane A up on the left of the object.
        for(int i=0;i<32;i++)state.vram_[64+i]=0xFF;        // tile 2: colour 15 everywhere
        for(int cy=0;cy<3;cy++)for(int cx=0;cx<4;cx++){const int a=state.planeABase()+(cy*state.planeWidthCells()+cx)*2;state.vram_[a]=0;state.vram_[a+1]=2;}
        state.regs_[0]|=4;state.regs_[7]=8;                 // full colour, a grey background: shadows show on it
        std::vector<uint16_t> unlit(640*448),lit(640*448);
        scene->lighting=false;assert(scene->build(state,renderer));sor::raster_enhanced(*scene,state,unlit.data(),640);
        scene->lighting=true;assert(scene->build(state,renderer)&&scene->artCount==1);
        const auto &d=scene->artDraws[0];
        assert(d.lit&&d.shadow&&d.ground==66);
        assert(scene->sceneLight().lightCount()>0&&scene->sceneLight().lights()[0].x<48);   // the block is a light
        assert(scene->sceneLight().spill(1).strength>scene->sceneLight().spill(12).strength);   // and spills on the ground below it
        const sor::ArtTint k[4]={d.light.corner(0),d.light.corner(1),d.light.corner(2),d.light.corner(3)};
        assert(d.light.column[1][0].scale[1]>d.light.column[3][0].scale[1]&&d.light.rim[0].alpha>d.light.rim[1].alpha);   // a rounded form, its lit edge
        assert(k[0].scale[1]>k[1].scale[1]&&k[0].offset[1]>k[1].offset[1]);   // the left is towards the light
        assert(k[2].scale[1]<k[0].scale[1]);                                   // the feet are darker
        assert(d.light.shadow[0].alpha>d.light.shadow[1].alpha&&d.light.shadow[0].lean>0&&d.light.shadow[0].length>0);   // the shadow runs away from it, to the right
        sor::raster_enhanced(*scene,state,lit.data(),640);
        const auto red=[](uint16_t c){return c>>10&31;};
        assert(red(lit[(66*2-1)*640+48*2+1])<red(unlit[(66*2-1)*640+48*2+1]));   // the art's far foot is darker
        bool shadow=false;
        for(int y=66*2;y<66*2+4;y++)for(int x=48*2-4;x<48*2+6;x++)shadow|=lit[y*640+x]!=unlit[y*640+x];
        assert(shadow);                                                        // darkened ground below the feet
        // A round's profile: the beach has the moon (its own shadow, leaning right) and no spill.
        assert(!d.light.shadow[2].alpha);
        scene->round=3;assert(scene->build(state,renderer));
        assert(scene->artDraws[0].light.shadow[2].alpha>0&&scene->artDraws[0].light.shadow[2].lean>0&&!scene->sceneLight().spill(1).strength);
        scene->round=0;assert(scene->build(state,renderer));
        // In the air (the level is less than the ground's) the shadow stays on the ground
        // line; a lone object's level counts as the ground only once it has been kept.
        place(140);
        assert(scene->build(state,renderer)&&scene->artDraws[0].ground==66+20);
        // The scene cache tells lit from unlit.
        scene->invalidate();
        assert(scene->buildCached(state,renderer)&&!scene->reused);
        assert(scene->buildCached(state,renderer)&&scene->reused);
        scene->lighting=false;
        assert(scene->buildCached(state,renderer)&&!scene->reused&&!scene->artDraws[0].lit);
        // Emitters add their colour to the corners near them; types outside the playfield are not lit.
        sor::CornerLight glow;
        sor::LightEmitter fire{100,50,64,{255,128,0},200};
        sor::SceneLight::glow(fire,40,40,90,60,glow);
        assert(glow.corner(1).offset[0]>glow.corner(0).offset[0]&&glow.corner(1).offset[2]==0&&glow.corner(1).offset[0]>glow.corner(1).offset[1]);
        // Fire throws embers: particles live in the scene, advance with the game's
        // builds, keep the scene from being reused, and end.
        {
            sor::Particles particles;
            for(int i=0;i<40;i++){particles.fire(100,200,0);particles.advance(1);}
            assert(particles.alive());
            sor::ParticleDraw draws[sor::Particles::MAX];
            const size_t n=particles.draw(0,320,224,draws);
            assert(n>0&&draws[0].additive&&draws[0].y<200*2);                  // above the flame's foot
            assert(particles.draw(5000,320,224,draws)==0);                     // scrolled away
            particles.advance(100);assert(!particles.alive());
            particles.burst(50,100,0);assert(particles.draw(0,320,224,draws)==7);
            particles.clear();particles.dust(50,100,0);
            assert(particles.draw(0,320,224,draws)==5&&!draws[0].additive);        // dust covers, light is added
            particles.clear();particles.debris(50,100,0);assert(particles.draw(0,320,224,draws)==14);
            particles.clear();particles.splash(50,100,0);assert(particles.draw(0,320,224,draws)==1&&draws[0].additive);
        }
        assert(sor::light_kind(0x57,0x02F3F3)==sor::LightKind::FIREBALL&&sor::light_kind(0x57,0x02F1A5)==sor::LightKind::NONE);   // a boss's breath, not the boss
        assert(sor::emits_light(0x0E,0)&&!sor::emits_light(0x05,0x070B20)&&sor::emits_light(0x05,0x070C1F)&&sor::light_kind(0x05,0x070C51)==sor::LightKind::ROCKET&&!sor::in_playfield(0x0E)&&sor::in_playfield(0x01)&&sor::in_playfield(0x24)&&!sor::in_playfield(0x54));
        for(int cy=0;cy<3;cy++)for(int cx=0;cx<4;cx++){const int a=state.planeABase()+(cy*state.planeWidthCells()+cx)*2;state.vram_[a+1]=0;}
        state.regs_[0]&=~4;state.regs_[7]=0;place(160);scene->invalidate();
    }

    // VRAM's table no longer matches the build: the object's pieces come back.
    state.vram_[state.satBase()+7]^=1;
    assert(!probe.displayed(state));
    assert(scene->build(state,renderer));
    assert(scene->artCount==0&&scene->spriteTileCount==5);
    puts("Enhanced scene: art replaces a probed object in its SAT slot (keyed by colours, per round); other sprites stay cells; stale builds fall back; in-between poses on a pose change; dynamic lighting tints per corner and casts shadows");
}
