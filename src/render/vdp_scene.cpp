#include "vdp_scene.hpp"
#include "equal_bytes.hpp"
#include <cmath>
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
    if(length>=65536)return equal_bytes(a.vram_,b.vram_,65536);
    base&=65535;int first=std::min(length,65536-base);
    return equal_bytes(a.vram_+base,b.vram_+base,first)
        && (first==length || equal_bytes(a.vram_,b.vram_,length-first));
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
bool VdpScene::buildCached(VDPState &s){
    // VRAM: no write since the cached frame (a write of equal bytes rebuilds).
    reused=cacheValid && enhanced==builtEnhanced_ && lighting==builtLighting_ && weather==builtWeather_ && round==builtRound_ && !inbetween_ && same_render_regs(s,previous)
        && s.vramGeneration_==previous.vramGeneration_
        && equal_bytes(s.cram_,previous.cram_,sizeof(s.cram_))
        && equal_bytes(s.vsram_,previous.vsram_,sizeof(s.vsram_))
        && equal_bytes(s.sat_,previous.sat_,sizeof(s.sat_));
    if(reused){
        // Particles and the weather move on their own: they do not make the
        // scene another. Lightning does: it lights everything.
        if(enhanced&&lighting){particlesStep(s,art&&!art->empty()?sprite_probe().displayed(s):nullptr);weatherStep();}
        if(weather_.flash()==builtFlash_){
            planesReused=true;
            s.status_|=spriteFlags;
            if(s.displayEnabled())s.vCounter_=height-1;
            return true;
        }
        reused=false;
    }
    int mapBytes=s.planeWidthCells()*s.planeHeightCells()*2;
    bool geometrySame=cacheValid && same_geometry_regs(s,previous)
        && equal_bytes(s.vsram_,previous.vsram_,sizeof(s.vsram_))
        && unchanged_region(s,previous,s.planeABase(),mapBytes)
        && unchanged_region(s,previous,s.planeBBase(),mapBytes)
        && unchanged_region(s,previous,s.windowBase(),(s.h40Mode()?64:32)*32*2)
        && unchanged_region(s,previous,s.hscrollBase(),s.hscrollMode()==0?4:s.activeHeight()*4);
    const auto status=s.status_;s.status_&=~0x60;
    builtEnhanced_=enhanced;builtLighting_=lighting;builtWeather_=weather;builtRound_=round;
    cacheValid=buildImpl(s,geometrySame);spriteFlags=s.status_&0x60;s.status_|=status;
    if(cacheValid)previous=s;
    return cacheValid;
}
bool VdpScene::build(VDPState &s){
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
    if(!keepPlanes){plane(s,1);planeEnd[0]=count;plane(s,0);planeEnd[1]=count;window(s);}
    // Enhanced drawing needs no software sprite layers. Those also produce the
    // VDP's sprite overflow and collision status bits, but the game never acts
    // on them: replays with both bits forced clear keep every frame's RAM equal.
    if(enhanced)enhancedSprites(s);
    else spriteLayers(s);
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
void VdpScene::enhancedSprites(const VDPState &s){
    spriteTileCount=0;artCount=0;glowCount=0;
    const int base=s.satBase();
    const auto record=[&](int index){return s.vram_+((base+index*8)&0xFFFF);};
    // SAT records owned by objects that have replacement art.
    int16_t owner[VDPState::SAT_MAX_SPRITES];std::fill_n(owner,VDPState::SAT_MAX_SPRITES,int16_t(-1));
    uint32_t frameOf[SpriteBuild::MAX_OBJECTS]{};ArtTint tintOf[SpriteBuild::MAX_OBJECTS];
    const SpriteBuild *build=art&&!art->empty()?sprite_probe().displayed(s):nullptr;
    // Builds since the poses were recorded. After a gap (scenes are not built
    // for every frame in host captures) a pose change is not a transition.
    const uint32_t elapsed=build?build->serial-poseSerial_:0;
    Pose now[SpriteBuild::MAX_OBJECTS];unsigned nowCount=0;
    if(build){
        if(elapsed>2)poseCount_=0;
        poseSerial_=build->serial;inbetween_=false;
    }
    if(build)for(unsigned o=0;o<build->count;o++){
        const auto &obj=build->objects[o];
        if(obj.first+obj.count>VDPState::SAT_MAX_SPRITES)continue;
        const uint16_t *line=s.cram_+(record(obj.first)[4]>>5&3)*16;
        ArtTint tint;
        const ArtFrame *f=art->find(obj.mapping,line,&tint);
        if(!f)continue;
        Pose pose{obj.set,obj.mapping,0,obj.slot,0,obj.flip};
        if(smooth)for(unsigned i=0;i<poseCount_;i++){
            const Pose &was=poses_[i];
            if(was.slot!=obj.slot||was.set!=obj.set||was.flip!=obj.flip)continue;
            if(was.mapping!=obj.mapping){pose.from=was.mapping;pose.ticks=INBETWEEN_TICKS;}
            else if(was.ticks>elapsed){pose.from=was.from;pose.ticks=uint8_t(was.ticks-elapsed);}
            break;
        }
        if(pose.ticks){
            ArtTint betweenTint;
            if(const ArtFrame *between=art->between(pose.from,obj.mapping,line,&betweenTint)){f=between;tint=betweenTint;inbetween_=true;}
            else pose.ticks=0;
        }
        now[nowCount++]=pose;
        frameOf[o]=uint32_t(f-art->frames().data());tintOf[o]=tint;
        for(int r=obj.first;r<obj.first+obj.count;r++)owner[r]=int16_t(o);
    }
    if(build){std::copy(now,now+nowCount,poses_);poseCount_=nowCount;}
    // Lighting: objects of the world are lit, whether drawn as art or as their
    // pieces; objects that are light shine on the others and on the ground.
    const bool lightOn=lighting&&build;
    light_.setRound(round);
    weatherQuadCount=0;
    if(lighting)particlesStep(s,build);else{particles_.clear();particleCount=0;weather_.clear();weatherActive_=false;}
    // Lightning: for a few ticks the sky is the light, and the bolt casts the shadows.
    builtFlash_=uint8_t(weatherOn()?weather_.flash():0);
    if(builtFlash_){
        const int f=builtFlash_;
        flashProfile_=light_.profile();
        flashProfile_.sky[0]=225;flashProfile_.sky[1]=232;flashProfile_.sky[2]=255;
        flashProfile_.skyTint=uint8_t(std::max<int>(flashProfile_.skyTint,f*3/4));
        flashProfile_.skyShare=uint8_t(std::max<int>(flashProfile_.skyShare,f));
        flashProfile_.skyLean=int8_t(weather_.lean());flashProfile_.skyLength=22;
        flashProfile_.shadowAlpha=uint8_t(std::min(255,flashProfile_.shadowAlpha+f/3));
        flashProfile_.unlit=uint8_t(std::min(255,flashProfile_.unlit+f/4));
        light_.setProfile(&flashProfile_);
    }
    LightEmitter emitters[MAX_EMITTERS];unsigned emitterCount=0;
    int16_t pieces[VDPState::SAT_MAX_SPRITES];std::fill_n(pieces,VDPState::SAT_MAX_SPRITES,int16_t(-1));
    // Standing in the playfield (lit, casting shadows), not a light itself.
    const auto inWorld=[](const ProbedObject &obj){return !obj.screen&&in_playfield(obj.type)&&light_kind(obj.type,obj.mapping)==LightKind::NONE;};
    if(lightOn){
        // The backdrop's light changes when it scrolls or its colours do; tiles
        // animating in place are caught a few builds later.
        // While it scrolls, every fourth build: light a few frames late does not show.
        lightAge_+=std::max<uint32_t>(1,std::min<uint32_t>(elapsed,16));   // builds of the game's, not scenes built (host captures skip frames)
        if(!lightValid_||lightAge_>=16||(lightAge_>=4&&(!planesReused||background!=lightBackground_||!equal_bytes(colors,lightColors_,sizeof colors)))){
            light_.build(quads,planeEnd[0],planeEnd[1],colors,background,s);
            std::memcpy(lightColors_,colors,sizeof colors);lightBackground_=background;lightValid_=true;lightAge_=0;lightCollected_=false;
        }
        int16_t best=0;unsigned bestCount=0;
        for(unsigned o=0;o<build->count;o++){
            const auto &obj=build->objects[o];
            if(!inWorld(obj))continue;
            unsigned same=0;
            for(unsigned k=0;k<build->count;k++)same+=inWorld(build->objects[k])&&build->objects[k].level==obj.level;
            if(same>bestCount||(same==bestCount&&obj.level>best)){best=obj.level;bestCount=same;}
        }
        const int16_t levelWas=groundLevel_;const bool knownWas=groundKnown_;
        if(bestCount>=2){groundLevel_=best;groundKnown_=true;loneBuilds_=0;}
        else if(bestCount==1){
            loneBuilds_=best==loneLevel_?loneBuilds_+elapsed:0;loneLevel_=best;
            if(loneBuilds_>=8||!groundKnown_){groundLevel_=best;groundKnown_=true;}
        }
        if(!knownWas||levelWas!=groundLevel_)farthestGround_=32767;        // another round, another ground
        for(unsigned o=0;o<build->count;o++){
            const auto &obj=build->objects[o];
            if(!inWorld(obj)||obj.level!=groundLevel_)continue;
            const int line=obj.y-128;
            if(line>64&&line<height)farthestGround_=int16_t(std::min<int>(farthestGround_,farthestGround_==32767?line-24:line));
        }
        // The ground meets the wall some lines above the farthest anyone walks.
        // Collecting the lights divides a lot: only when the grid, the wall line or the round is another.
        const int wall=farthestGround_==32767?height*5/8:farthestGround_-WALL_ABOVE_LANES;
        if(!lightCollected_||wall!=light_.wallLine()||round!=collectedRound_){light_.collect(wall);lightCollected_=true;collectedRound_=round;}
        // Weather: the wall's lights glow through the fog.
        if(weatherOn()&&weatherProfile().shafts&&weatherProfile().fog)
            for(unsigned i=0;i<light_.lightCount()&&glowCount<MAX_GLOWS;i++){
                const auto &l=light_.lights()[i];
                if(l.low||l.power<2500)continue;
                const int norm=std::min(255,int(l.power/128)),radius=16+norm/10;
                glows[glowCount++]={l.x,l.y,int16_t(radius),int16_t(radius),{l.colour[0],l.colour[1],l.colour[2]},uint8_t(weatherProfile().shafts*norm/255*weatherProfile().fog/255*60/255)};
            }
        // Lamps on the ground put a small pool of their light around them.
        for(unsigned i=0;i<light_.lightCount()&&glowCount<MAX_GLOWS;i++){
            const auto &l=light_.lights()[i];
            if(l.low)glows[glowCount++]={l.x,int16_t(l.y+6),40,16,{l.colour[0],l.colour[1],l.colour[2]},uint8_t(std::min(70u,unsigned(l.power)/80))};
        }
        for(unsigned o=0;o<build->count&&emitterCount<MAX_EMITTERS;o++){
            const auto &obj=build->objects[o];
            if(obj.screen||!emits_light(obj.type,obj.mapping)||obj.first+obj.count>VDPState::SAT_MAX_SPRITES)continue;
            // Fire (the police's napalm) reaches far and lights the ground; the bazooka's flame less; sparks are small.
            const bool fire=obj.type==0x0E,ball=!fire&&obj.type!=0x49;
            LightEmitter e{int16_t(obj.x-128),int16_t(obj.y-128-(fire?24:8)),int16_t(fire?112:ball?80:48),{},uint8_t(fire?120:ball?110:80)};
            const bool hasArt=owner[obj.first]==int16_t(o);
            emitter_colour(s.cram_+(record(obj.first)[4]>>5&3)*16,hasArt?art->frames()[frameOf[o]].mask:0xFFFE,e.colour);
            emitters[emitterCount++]=e;
            // It lights the wall behind it too: a wide faint glow around the flame itself.
            if(obj.type!=0x49&&glowCount<MAX_GLOWS)
                glows[glowCount++]={e.x,e.y,int16_t(e.radius*3/4),int16_t(e.radius*3/4),{e.colour[0],e.colour[1],e.colour[2]},uint8_t(fire?26:60)};
            if(obj.type!=0x49&&glowCount<MAX_GLOWS)
                glows[glowCount++]={int16_t(obj.x-128),int16_t(obj.y-128+std::max(0,groundLevel_-obj.level)),int16_t(e.radius),int16_t(e.radius*3/8),{e.colour[0],e.colour[1],e.colour[2]},uint8_t(e.strength/3)};
        }
        for(unsigned o=0;o<build->count;o++){
            const auto &obj=build->objects[o];
            if(obj.first+obj.count>VDPState::SAT_MAX_SPRITES||owner[obj.first]==int16_t(o)||!inWorld(obj))continue;
            for(int r=obj.first;r<obj.first+obj.count;r++)pieces[r]=int16_t(o);
        }
    }
    const auto lightOf=[&](int x0,int y0,int x1,int y1,int ground,const ArtTint &tint,CornerLight &out){
        light_.shade(x0,y0,x1,y1,ground,out);
        SceneLight::GlowSum sum;
        for(unsigned i=0;i<emitterCount;i++)SceneLight::glow(emitters[i],x0,y0,x1,y1,sum);
        SceneLight::addGlow(sum,out);
        SceneLight::apply(tint,out);
    };
    // Sprite masking, as in spriteLayers: a sprite at x = 0 blanks every later
    // sprite on its lines, once a sprite with another x has been on the line.
    // The game hides whatever passes behind the HUD this way (a player
    // dropping in at the start of a round).
    bool seenX[256]{},masked[256]{};
    uint8_t shade[3]{255,255,255},glow[3]{};int shadeOwner=-1;
    int index=0;
    for(int ordinal=0;ordinal<VDPState::SAT_MAX_SPRITES;ordinal++){
        const uint8_t *e=record(index);
        const int link=e[3]&127,attr=e[4]<<8|e[5],layer=attr>>15&1;
        {
            const int top=((e[0]&3)<<8|e[1])-128,rows=((e[2]&3)+1)*8;
            const bool zero=!(((e[6]&1)<<8)|e[7]);
            for(int line=std::max(0,top);line<std::min(height,top+rows);line++){
                if(!zero)seenX[line]=true;else if(seenX[line])masked[line]=true;
            }
        }
        if(owner[index]>=0){
            const auto &obj=build->objects[owner[index]];
            if(index==obj.first){
                // The art's longest run of lines that are not blanked.
                const ArtFrame &f=art->frames()[frameOf[owner[index]]];
                const int top=obj.y-128-(f.anchorY+1)/2,bottom=top+(f.h+1)/2;
                int from=0,to=0,run=-1;
                for(int line=top;line<=bottom;line++){
                    const bool open=line<bottom&&!(line>=0&&line<height&&masked[line]);
                    if(open&&run<0)run=line;
                    if(!open&&run>=0){if(line-run>to-from){from=run;to=line;}run=-1;}
                }
                if(from==top)from=-32768;
                if(to==bottom)to=32767;
                if(to>from){
                    ArtDraw &d=artDraws[artCount++];
                    d={frameOf[owner[index]],int16_t(obj.x-128),int16_t(obj.y-128),uint8_t(layer),uint8_t(ordinal),obj.flip,tintOf[owner[index]],int16_t(from),int16_t(to),obj.type};
                    if(lightOn&&inWorld(obj)){
                        const int left=d.x-((obj.flip?f.w-f.anchorX:f.anchorX)+1)/2;
                        d.ground=int16_t(d.y+std::max(0,groundLevel_-obj.level));
                        lightOf(left,top,left+(f.w+1)/2,bottom,d.ground,d.tint,d.light);
                        d.lit=true;
                        // A fading object's shadows fade with it.
                        d.shadow=from==-32768&&to==32767;   // dropping in behind the HUD (a sprite mask): none
                        for(auto &shadow:d.light.shadow)shadow.alpha=uint8_t(shadow.alpha*(d.tint.scale[0]+d.tint.scale[1]+d.tint.scale[2])/765);
                    }
                }
            }
        }else{
            const int y=((e[0]&3)<<8|e[1])-128,x=((e[6]&1)<<8|e[7])-128;
            const int cellsW=(e[2]>>2&3)+1,cellsH=(e[2]&3)+1;
            const bool hf=attr&0x800,vf=attr&0x1000;
            if(x<width&&y<height&&x+cellsW*8>0&&y+cellsH*8>0)
                for(int cx=0;cx<cellsW;cx++)for(int cy=0;cy<cellsH;cy++){
                    if(spriteTileCount==MAX_SPRITE_TILES)break;
                    const int dx=(hf?cellsW-1-cx:cx)*8,dy=(vf?cellsH-1-cy:cy)*8;
                    const int middle=y+dy+4;
                    if(middle>=0&&middle<height&&masked[middle])continue;   // blanked by a sprite mask
                    spriteTiles[spriteTileCount++]={uint16_t((attr+cx*cellsH+cy)&2047),int16_t(x+dx),int16_t(y+dy),
                        uint8_t(attr>>13&3),uint8_t(layer),uint8_t(ordinal),hf,vf};
                    if(pieces[index]>=0){
                        // An object drawn from its pieces: one light for them all, no added light
                        // (tile quads have no offset colour).
                        const auto &obj=build->objects[pieces[index]];
                        if(shadeOwner!=pieces[index]){
                            CornerLight c;lightOf(obj.x-128-16,obj.y-128-64,obj.x-128+16,obj.y-128,obj.y-128+std::max(0,groundLevel_-obj.level),ArtTint{},c);
                            for(int k=0;k<3;k++){shade[k]=uint8_t((c.column[2][0].scale[k]+c.column[2][1].scale[k])/2);glow[k]=uint8_t((c.column[2][0].offset[k]+c.column[2][1].offset[k])/2);}
                            shadeOwner=pieces[index];
                        }
                        std::copy(shade,shade+3,spriteTiles[spriteTileCount-1].shade);std::copy(glow,glow+3,spriteTiles[spriteTileCount-1].glow);
                    }
                }
        }
        if(!link||link>=VDPState::SAT_MAX_SPRITES)break;
        index=link;
    }
    weatherStep();
}
void VdpScene::weatherStep(){
    weatherQuadCount=0;
    if(!weatherOn()||!weatherProfile().any())return;
    weatherQuadCount=weather_quads(weatherProfile(),weather_,camera_,width,height,light_.wallLine(),light_.lights(),light_.lightCount(),weatherQuads);
}
void VdpScene::particlesStep(const VDPState &s,const SpriteBuild *build){
    // A tick of theirs per sprite-table build of the game's; emitters feed them.
    // They stop with the game (no build displayed: a menu, a cutscene).
    const uint32_t elapsed=build?build->serial-particleSerial_:0;
    // Paused (no build for a while): the weather stands still; no build at all (a
    // menu, a cutscene): there is none.
    weatherActive_=build!=nullptr;
    if(!build||elapsed>30){particles_.clear();particleCount=0;trackedCount_=0;if(build)particleSerial_=build->serial;else weather_.clear();return;}
    particleSerial_=build->serial;
    const unsigned ticks=std::min<uint32_t>(elapsed,3);
    const int camera=-scroll(s,0,std::min(height-1,std::max(0,int(light_.wallLine())+8)));
    camera_=camera;
    particles_.advance(ticks);
    if(weatherOn())weather_.advance(ticks,weatherProfile());else weather_.clear();
    uint16_t sparks[16];unsigned sparkCount=0;
    for(unsigned o=0;o<build->count;o++){
        const auto &obj=build->objects[o];
        const LightKind kind=light_kind(obj.type,obj.mapping);
        if(kind==LightKind::NONE||obj.screen)continue;
        const int x=obj.x-128,y=obj.y-128;
        for(unsigned t=0;t<ticks;t++)switch(kind){
        case LightKind::FIRE:particles_.fire(x,y,camera);break;
        case LightKind::FIREBALL:particles_.fireball(x,y,camera);break;
        case LightKind::ROCKET:particles_.rocket(x,y,camera);break;
        default:break;
        }
        if(kind==LightKind::HIT_SPARK){
            bool known=false;
            for(unsigned i=0;i<sparkSlotCount_;i++)known|=sparkSlots_[i]==obj.slot;
            if(!known&&ticks)particles_.burst(x,y,camera);
            if(sparkCount<16)sparks[sparkCount++]=obj.slot;
        }
    }
    if(ticks){
        std::copy(sparks,sparks+sparkCount,sparkSlots_);sparkSlotCount_=sparkCount;
        // Feet coming down raise dust; a prop that was on screen and is gone has broken; rain lands.
        const auto prop=[](unsigned type){return type==0x11||type==0x18||type==0x19||type==0x1B||type==0x1F||type==0x41;};
        bool rain=false;
        for(unsigned o=0;o<build->count;o++){
            const auto &obj=build->objects[o];
            rain|=obj.type==0x17;
            if(obj.screen||!in_playfield(obj.type)||prop(obj.type)||obj.level!=groundLevel_||!groundKnown_)continue;
            for(unsigned i=0;i<trackedCount_;i++)
                if(tracked_[i].slot==obj.slot&&tracked_[i].type==obj.type&&tracked_[i].level<groundLevel_-3)particles_.dust(obj.x-128,obj.y-128,camera);
        }
        for(unsigned i=0;i<trackedCount_;i++){
            const Tracked &was=tracked_[i];
            if(!prop(was.type)||was.x<16||was.x>=width-16)continue;
            bool there=false;
            for(unsigned o=0;o<build->count;o++)there|=build->objects[o].slot==was.slot&&build->objects[o].type==was.type;
            if(!there)particles_.debris(was.x,was.y-16,camera);
        }
        // The game's rain and the weather's land on the ground.
        const unsigned drops=(rain?2u:0u)+(weatherOn()?weatherProfile().rain*3u/255u:0u);
        if(drops)for(unsigned t=0;t<ticks*drops;t++){
            const int top=std::min(height-8,light_.wallLine()+20);
            particles_.splash(int(particles_.random()%unsigned(width)),top+int(particles_.random()%unsigned(height-4-top)),camera);
        }
        trackedCount_=0;
        for(unsigned o=0;o<build->count;o++){const auto &obj=build->objects[o];tracked_[trackedCount_++]={obj.slot,int16_t(obj.x-128),int16_t(obj.y-128),obj.level,obj.type};}
    }
    particleCount=particles_.draw(camera,width,height,particleDraws);
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
void raster_enhanced(const VdpScene &scene,const VDPState &s,uint16_t *out,int pitch){
    const int w=scene.width*2,h=scene.height*2;
    for(int y=0;y<h;y++)std::fill_n(out+y*pitch,w,scene.background);
    const auto plot=[&](int x,int y,uint16_t c){if(x>=0&&y>=0&&x<w&&y<h)out[y*pitch+x]=c;};
    const auto tileTexel=[&](int tile,int u,int v){uint8_t b=s.vram_[(tile*32+v*4+u/2)&0xFFFF];return (u&1)?b&15:b>>4;};
    const auto channel=[](uint16_t c,int ch){return int(c>>(10-ch*5)&31);};
    const auto blend=[&](int x,int y,const int add[3],int keep){   // out = out*keep/255 + add (5-bit channels)
        if(x<0||y<0||x>=w||y>=h)return;
        uint16_t &pixel=out[y*pitch+x],result=0x8000;
        for(int ch=0;ch<3;ch++)result|=uint16_t(std::min(31,channel(pixel,ch)*keep/255+add[ch])<<(10-ch*5));
        pixel=result;
    };
    // A quad of weather: horizontal top and bottom edges, texture and alpha
    // interpolated between the corners, added or covering.
    const auto weatherQuad=[&](const WeatherQuad &q){
        const int y0=q.y[0],y1=q.y[2];
        if(y1<=y0)return;
        for(int y=std::max(0,y0);y<std::min(h,y1);y++){
            const int t=(y-y0)*256/(y1-y0);
            const int xl=q.x[0]+(q.x[2]-q.x[0])*t/256,xr=q.x[1]+(q.x[3]-q.x[1])*t/256;
            if(xr<=xl)continue;
            const int ul=q.u[0]*16+(q.u[2]-q.u[0])*16*t/256,ur=q.u[1]*16+(q.u[3]-q.u[1])*16*t/256;
            const int vl=q.v[0]*16+(q.v[2]-q.v[0])*16*t/256,vr=q.v[1]*16+(q.v[3]-q.v[1])*16*t/256;
            const int al=q.alpha[0]+(q.alpha[2]-q.alpha[0])*t/256,ar=q.alpha[1]+(q.alpha[3]-q.alpha[1])*t/256;
            for(int x=std::max(0,xl);x<std::min(w,xr);x++){
                const int s=(x-xl)*256/(xr-xl);
                const int alpha=(al+(ar-al)*s/256)*weather_sample(q.texture,ul+(ur-ul)*s/256,vl+(vr-vl)*s/256)/255;
                if(!alpha)continue;
                const int add[3]={q.colour[0]*alpha/255*31/255,q.colour[1]*alpha/255*31/255,q.colour[2]*alpha/255*31/255};
                blend(x,y,add,q.additive?255:255-alpha);
            }
        }
    };
    const auto weatherAt=[&](WeatherQuad::Depth depth){for(size_t i=0;i<scene.weatherQuadCount;i++)if(scene.weatherQuads[i].depth==depth)weatherQuad(scene.weatherQuads[i]);};
    for(int depth=1;depth<=6;depth++){
        if(depth==2)weatherAt(WeatherQuad::BEHIND);
        if(depth==3){
            // Lighting, on the ground: over the low planes, under every sprite
            // and the high-priority tiles. Pools of light are added; shadows are
            // the art again, black, sheared from the ground line towards the viewer.
            for(size_t i=0;i<scene.glowCount;i++){const auto &g=scene.glows[i];
                const int rx=g.radiusX*2,ry=g.radiusY*2;
                for(int y=-ry;y<ry;y++)for(int x=-rx;x<rx;x++){
                    const int distance=int(std::sqrt(double(x)*x/(double(rx)*rx)+double(y)*y/(double(ry)*ry))*255);
                    if(distance>=255)continue;
                    const int amount=(255-distance)*(255-distance)/255*g.strength/255;
                    const int add[3]={g.colour[0]*amount/255*31/255,g.colour[1]*amount/255*31/255,g.colour[2]*amount/255*31/255};
                    blend(g.x*2+x,g.y*2+y,add,255);
                }
            }
            // Light spilt by the backdrop's lights onto the ground below the wall line,
            // fading over SPILL_DEPTH lines.
            if(scene.lighting){
                const auto &light=scene.sceneLight();
                // From nothing SPILL_RISE lines above the wall line (where the ground meets
                // the wall is not known to a line) to full at the wall line, then fading.
                const int rise=SceneLight::SPILL_RISE*2,top=light.wallLine()*2-rise,depthLines=SceneLight::SPILL_DEPTH*2+rise;
                for(int y=0;y<depthLines;y++)for(int x=0;x<w;x++){
                    const int column=x/(SceneLight::CELL*2),fx=x%(SceneLight::CELL*2)*256/(SceneLight::CELL*2);
                    if(column>=SceneLight::COLS)break;
                    const auto &a=light.spill(column),&b=light.spill(column+1);
                    const int fade=y<rise?y*255/rise:(depthLines-y)*255/(depthLines-rise);
                    int add[3];
                    for(int k=0;k<3;k++)add[k]=((a.colour[k]*a.strength*(256-fx)+b.colour[k]*b.strength*fx)>>8)/255*fade/255*31/255;
                    blend(x,top+y,add,255);
                }
            }
            for(size_t i=0;i<scene.artCount;i++){const auto &d=scene.artDraws[i];
                if(!d.shadow||!scene.art)continue;
                const auto &f=scene.art->frames()[d.frame];const auto &page=scene.art->pages()[f.page];
                const int left=d.x*2-(d.flip?f.w-f.anchorX:f.anchorX);
                const int none[3]{};
                // Contact shadow: a dark ellipse under the feet, smaller the higher the object is.
                {
                    const int rx=std::max(6,f.w*VdpScene::contactScale(d.ground-d.y)/256/2),ry=std::max(3,rx/4),cx=left+f.w/2,cy=d.ground*2;
                    for(int y=-ry;y<ry;y++)for(int x=-rx;x<rx;x++){
                        const int distance=int(std::sqrt(double(x)*x/(double(rx)*rx)+double(y)*y/(double(ry)*ry))*255);
                        if(distance<255)blend(cx+x,cy+y,none,255-(255-distance)*(255-distance)/255*VdpScene::CONTACT_ALPHA/255);
                    }
                }
                for(const auto &shadow:d.light.shadow){
                    if(!shadow.alpha)continue;
                    const int length=shadow.length;
                    for(int row=-(f.h-f.anchorY)*length/64;row<f.anchorY*length/64;row++){
                        const int above=row*64/length,v=f.anchorY-1-above;   // art row this ground row shows
                        if(v<0||v>=f.h)continue;
                        for(int u=0;u<f.w;u++){
                            const uint16_t c=scene.art->texel(page,size_t(f.v+v)*page.width+f.u+(d.flip?f.w-1-u:u));
                            if(c&0x8000)blend(left+u+above*shadow.lean/64,d.ground*2+row,none,255-shadow.alpha);
                        }
                    }
                }
                // Wet ground: the art again, mirrored in the ground line, dimmed, fading
                // away from the feet and with height.
                if(const int reflect=VdpScene::reflectAlpha(scene.reflectAlpha(),d.ground-d.y)){
                    const int rows=f.anchorY*REFLECT_LENGTH/64,top=(2*d.ground-d.y)*2;
                    for(int row=0;row<rows;row++){
                        const int v=f.anchorY-1-row*64/REFLECT_LENGTH,alpha=reflect*(rows-row)/rows;
                        if(v<0)break;
                        for(int u=0;u<f.w;u++){
                            const uint16_t c=scene.art->texel(page,size_t(f.v+v)*page.width+f.u+(d.flip?f.w-1-u:u));
                            if(!(c&0x8000))continue;
                            int add[3];for(int ch=0;ch<3;ch++)add[ch]=channel(c,ch)*REFLECT_TINT[ch]/255*alpha/255;
                            blend(left+u,top+row,add,255-alpha);
                        }
                    }
                }
            }
            weatherAt(WeatherQuad::GROUND);
        }
        if(depth==3||depth==6){
            const int layer=depth==6;
            // Back to front: the last sprite in link order is drawn first.
            for(int ordinal=VDPState::SAT_MAX_SPRITES-1;ordinal>=0;ordinal--){
                for(size_t i=0;i<scene.spriteTileCount;i++){const auto &t=scene.spriteTiles[i];
                    if(t.layer!=layer||t.order!=ordinal)continue;
                    for(int v=0;v<8;v++)for(int u=0;u<8;u++){
                        const int c=tileTexel(t.tile,t.hflip?7-u:u,t.vflip?7-v:v);
                        if(!c)continue;
                        uint16_t color=scene.colors[t.palette*16+c];
                        if(t.shade[0]!=255||t.shade[1]!=255||t.shade[2]!=255||t.glow[0]||t.glow[1]||t.glow[2])
                            color=uint16_t(0x8000|std::min(31,channel(color,0)*t.shade[0]/255+t.glow[0]*31/255)<<10|std::min(31,channel(color,1)*t.shade[1]/255+t.glow[1]*31/255)<<5|std::min(31,channel(color,2)*t.shade[2]/255+t.glow[2]*31/255));
                        for(int k=0;k<4;k++)plot((t.x+u)*2+(k&1),(t.y+v)*2+(k>>1),color);
                    }
                }
                for(size_t i=0;i<scene.artCount;i++){const auto &d=scene.artDraws[i];
                    if(d.layer!=layer||d.order!=ordinal||!scene.art)continue;
                    const auto &f=scene.art->frames()[d.frame];const auto &page=scene.art->pages()[f.page];
                    const int left=d.x*2-(d.flip?f.w-f.anchorX:f.anchorX),top=d.y*2-f.anchorY;
                    // Rim light: the art again in the light's colour, moved towards the
                    // lights, behind the art: what shows is a thin lit edge.
                    if(d.lit)for(int g=0;g<2;g++){
                        const auto &rim=d.light.rim[g];
                        if(!rim.alpha)continue;
                        const int add[3]={rim.colour[0]*rim.alpha/255*31/255,rim.colour[1]*rim.alpha/255*31/255,rim.colour[2]*rim.alpha/255*31/255};
                        for(int v=0;v<f.h;v++)for(int u=0;u<f.w;u++){
                            if(top+v<d.lineFrom*2||top+v>=d.lineTo*2)continue;
                            const uint16_t c=scene.art->texel(page,size_t(f.v+v)*page.width+f.u+(d.flip?f.w-1-u:u));
                            if(c&0x8000)blend(left+u+(g?VdpScene::RIM_SHIFT:-VdpScene::RIM_SHIFT),top+v-1,add,255-rim.alpha);
                        }
                    }
                    for(int v=0;v<f.h;v++)for(int u=0;u<f.w;u++){
                        if(top+v<d.lineFrom*2||top+v>=d.lineTo*2)continue;   // sprite mask
                        const uint16_t c=scene.art->texel(page,size_t(f.v+v)*page.width+f.u+(d.flip?f.w-1-u:u));
                        if(!(c&0x8000))continue;
                        if(d.lit?d.light.identity():d.tint.identity()){plot(left+u,top+v,c);continue;}
                        // As the PowerVR does: texture * vertex colour + offset colour,
                        // both interpolated between the quad's corners when it is lit.
                        uint16_t out=0x8000;
                        // Between the two columns of vertices around u, and top and bottom.
                        const int across=f.w>1?u*256*(CornerLight::COLUMNS-1)/(f.w-1):0,column=std::min(across>>8,CornerLight::COLUMNS-2);
                        const int fx=across-column*256,fy=f.h>1?v*256/(f.h-1):0;
                        for(int ch=0;ch<3;ch++){
                            const int shift=10-ch*5,value=(c>>shift&31)*255/31;
                            int scale=d.tint.scale[ch],offset=d.tint.offset[ch];
                            if(d.lit){
                                const auto &a=d.light.column[column],&b=d.light.column[column+1];
                                scale=((a[0].scale[ch]*(256-fx)+b[0].scale[ch]*fx)*(256-fy)+(a[1].scale[ch]*(256-fx)+b[1].scale[ch]*fx)*fy)>>16;
                                offset=((a[0].offset[ch]*(256-fx)+b[0].offset[ch]*fx)*(256-fy)+(a[1].offset[ch]*(256-fx)+b[1].offset[ch]*fx)*fy)>>16;
                            }
                            out|=uint16_t(std::min(255,value*scale/255+offset)*31/255<<shift);
                        }
                        plot(left+u,top+v,out);
                    }
                }
            }
        }
        for(size_t i=0;i<scene.count;i++){const auto &q=scene.quads[i];if(q.depth!=depth)continue;
            // Weather: haze on the backdrop's planes (not the HUD's window), by height.
            const bool haze=scene.weatherOn()&&i<scene.planeEnd[1];
            const int f0=haze?scene.fogAt(q.y,i<scene.planeEnd[0]):0,f1=haze?scene.fogAt(q.y+q.h,i<scene.planeEnd[0]):0;
            for(int y=0;y<q.h;y++)for(int x=0;x<q.w;x++){
                const int u=q.u1>q.u0?q.u0+x:q.u0-1-x,v=q.v1>q.v0?q.v0+y:q.v0-1-y;
                const int c=tileTexel(q.tile,u,v);
                if(!c)continue;
                uint16_t color=scene.colors[q.palette*16+c];
                if(f0||f1){
                    const int f=f0+(f1-f0)*(y*2+1)/(q.h*2);
                    const uint8_t *fog=scene.weatherProfile().fogColour;
                    const uint16_t was=color;color=0x8000;
                    for(int ch=0;ch<3;ch++)color|=uint16_t(std::min(31,channel(was,ch)*(255-f)/255+fog[ch]*f/255*31/255)<<(10-ch*5));
                }
                for(int k=0;k<4;k++)plot((q.x+x)*2+(k&1),(q.y+y)*2+(k>>1),color);
            }
        }
    }
    weatherAt(WeatherQuad::FRONT);
    // Particles, over everything: light is added, smoke covers.
    for(size_t i=0;i<scene.particleCount;i++){const auto &p=scene.particleDraws[i];
        const int r=p.radius;
        for(int y=-r;y<r;y++)for(int x=-r;x<r;x++){
            const int distance=int(std::sqrt(double(x*x+y*y))*255/r);
            if(distance>=255)continue;
            const int amount=(255-distance)*(255-distance)/255*p.alpha/255;
            const int add[3]={p.colour[0]*amount/255*31/255,p.colour[1]*amount/255*31/255,p.colour[2]*amount/255*31/255};
            blend(p.x+x,p.y+y,add,p.additive?255:255-amount);
        }
    }
}
}
