#include "scene_light.hpp"
#include "vdp_scene.hpp"
#include <algorithm>
namespace sor {
namespace {
int luminance(const uint8_t *c){return (c[0]*2+c[1]*5+c[2])/8;}
// How bright a colour is as a light: a saturated lamp (an orange window) is
// as much a light as a pale one, which luminance would not say.
int brightness(const uint8_t *c){return std::max({c[0],c[1],c[2]});}
int isqrt(int v){int r=0;for(int bit=1<<14;bit;bit>>=1)if((r+bit)*(r+bit)<=v)r+=bit;return r;}
// A colour's hue at full brightness.
void hue(const uint8_t *c,int out[3]){
    const int top=std::max({int(c[0]),int(c[1]),int(c[2]),1});
    for(int i=0;i<3;i++)out[i]=c[i]*255/top;
}
// White pulled towards a hue: strength 0-256.
int towards(int hueChannel,int strength){return 255-(255-hueChannel)*strength/256;}
// How strongly the light's colour shows, the unlit side's brightness, the
// feet's, and the most the lit side adds (of 255).
constexpr int FEET=238,GROUND_TINT=32,ADDED=46;
// The backdrop beside an object that counts as lighting it.
}
const LightProfile &light_profile(unsigned round){
    //                       exposure shadow spill ambient light unlit rim   sky              tint lean length share
    static const LightProfile profiles[9]={
        /* default */        { 900,    150,  255,  24,     52,   226,  150, {255,255,255},   0,   0,   18,    0},
        /* 1 street */       { 900,    150,  255,  24,     52,   226,  150, {255,255,255},   0,   0,   18,    0},
        /* 2 inner city */   { 500,    165,  200,  20,     60,   214,  170, {170,190,255},   24,  10,  16,    60},
        /* 3 beach */        { 700,    140,  0,    16,     30,   222,  120, {150,180,255},   56,  38,  30,    200},   // the moon, up on the left
        /* 4 bridge */       { 700,    150,  0,    20,     40,   220,  130, {190,210,255},   36, -24,  24,    150},
        /* 5 ship */         { 900,    140,  150,  24,     56,   228,  140, {255,225,180},   20,  0,   16,    80},
        /* 6 factory */      { 600,    185,  120,  18,     64,   204,  200, {255,240,210},   12,  0,   14,    90},    // harsh work lights
        /* 7 lift */         { 800,    150,  0,    20,     48,   220,  160, {200,215,255},   28,  0,   14,    130},   // no wall to spill from
        /* 8 headquarters */ { 900,    150,  110,  30,     56,   226,  150, {255,200,170},   24,  0,   16,    90},
    };
    return profiles[round<=8?round:0];
}
LightKind light_kind(unsigned type,uint32_t mapping){
    // The police's napalm; hit sparks. Type $05 is the police car ($070B20-), the
    // rocket it fires ($070C47-$070C5B, grey: no light, but smoke) and the
    // fireballs that come down ($070C1F- otherwise).
    if(type==0x0E)return LightKind::FIRE;
    if(type==0x49)return LightKind::HIT_SPARK;
    if(type==0x05&&mapping>=0x070C1F&&mapping<0x070D00)return mapping>=0x070C47&&mapping<=0x070C5B?LightKind::ROCKET:LightKind::FIREBALL;
    // The fire the round 6 bosses (and their return in round 8) breathe: frames of their own type.
    if((type==0x57||type==0x97)&&mapping>=0x02F3B2&&mapping<=0x02F42F)return LightKind::FIREBALL;
    return LightKind::NONE;
}
bool in_playfield(unsigned type){
    // Players, enemies and bosses; props and things lying on the ground. Not scenery
    // made of sprites (awnings, rain), captions or effects.
    if(type==0x01||(type>=0x20&&type<=0x3F)||(type>=0x55&&type<=0x58)||type==0x97)return true;
    switch(type){
    case 0x08:case 0x09:case 0x0A:case 0x0B:case 0x11:case 0x15:case 0x18:case 0x19:case 0x1B:case 0x1F:
    case 0x3F:case 0x40:case 0x41:case 0x44:case 0x45:case 0x47:case 0x4B:case 0x4C:return true;
    }
    return false;
}
void emitter_colour(const uint16_t *line,uint16_t mask,uint8_t rgb[3]){
    long sum[3]{},weight=0;
    for(int i=1;i<16;i++){
        if(!(mask>>i&1))continue;
        const uint8_t c[3]={uint8_t((line[i]>>1&7)*255/7),uint8_t((line[i]>>5&7)*255/7),uint8_t((line[i]>>9&7)*255/7)};
        const long w=long(luminance(c))*luminance(c);
        for(int k=0;k<3;k++)sum[k]+=c[k]*w;
        weight+=w;
    }
    const uint8_t warm[3]={255,176,80};
    for(int k=0;k<3;k++)rgb[k]=weight?uint8_t(sum[k]/weight):warm[k];
}
void SceneLight::build(const TileQuad *quads,size_t endB,size_t endA,const uint16_t *colors,uint16_t background,const VDPState &s){
    // Per cell and plane: colour sums over opaque samples, their weight, all
    // samples' weight; and the same with each sample counted by its brightness
    // squared, so that a few lit texels (a lamp) make a cell a light.
    // This runs whenever the backdrop scrolls: no division per quad.
    struct Sum {uint32_t colour[3],opaque,all,bright[3],energy;};
    static Sum sums[2][ROWS][COLS];
    for(auto &plane:sums)for(auto &row:plane)for(auto &c:row)c=Sum{};
    uint8_t rgb[64][3];uint16_t squared[64];
    for(int i=0;i<64;i++){
        rgb[i][0]=uint8_t((colors[i]>>10&31)*255/31);rgb[i][1]=uint8_t((colors[i]>>5&31)*255/31);rgb[i][2]=uint8_t((colors[i]&31)*255/31);
        const int b=brightness(rgb[i]);squared[i]=uint16_t(b*b>>6);
    }
    for(size_t i=0;i<endA;i++){
        const TileQuad &q=quads[i];
        const unsigned row=unsigned(q.y+q.h/2)/CELL;
        if(row>=unsigned(ROWS))continue;
        // Every other tile (this loop is the cost of lighting: a millisecond over
        // all tiles on the Dreamcast), chosen by pattern number: neighbours in a
        // picture mostly alternate, and the choice does not change as it scrolls
        // (a choice by place on screen would make the light shimmer).
        if(q.tile&1)continue;
        // Two texels of the tile stand for it (alternating between tiles, so
        // that a dither pattern is not sampled on one of its colours only); its
        // weight is its area on screen, shared between the two nearest columns so
        // that scrolling moves the light smoothly.
        const unsigned odd=q.tile>>1&1;
        const uint8_t *tile=s.vram_+((q.tile*32u)&0xFFE0);
        const unsigned index[2]={unsigned(odd?tile[5]>>4:tile[4]&15),unsigned(odd?tile[22]&15:tile[27]>>4)};   // (2,1) or (1,1); (5,5) or (6,6)
        unsigned colour[3]{},bright[3]{},energy=0,opaque=0;
        for(unsigned texel:index){
            if(!texel)continue;
            const unsigned entry=q.palette*16+texel,e=squared[entry];
            const uint8_t *c=rgb[entry];
            colour[0]+=c[0];colour[1]+=c[1];colour[2]+=c[2];opaque++;
            bright[0]+=c[0]*e;bright[1]+=c[1]*e;bright[2]+=c[2]*e;energy+=e;
        }
        const int area=q.w*q.h,position=((q.x+q.w/2)<<8)/CELL-128;   // 8.8 columns, from column centres
        const int first=position>>8,fraction=position&255;
        for(int side=0;side<2;side++){
            const int column=std::clamp(first+side,0,COLS-1);
            const unsigned weight=unsigned(area*(side?fraction:256-fraction))>>7;   // half a sample's
            Sum &sum=sums[i<endB?0:1][row][column];
            sum.colour[0]+=colour[0]*weight;sum.colour[1]+=colour[1]*weight;sum.colour[2]+=colour[2]*weight;
            sum.opaque+=opaque*weight;sum.all+=2*weight;
            sum.bright[0]+=(bright[0]>>4)*weight;sum.bright[1]+=(bright[1]>>4)*weight;sum.bright[2]+=(bright[2]>>4)*weight;
            sum.energy+=energy*weight;
        }
    }
    const uint8_t backdrop[3]={uint8_t((background>>10&31)*255/31),uint8_t((background>>5&31)*255/31),uint8_t((background&31)*255/31)};
    uint32_t total[3]{},level[ROWS][COLS],levels=0;
    for(int y=0;y<ROWS;y++)for(int x=0;x<COLS;x++){
        const Sum &b=sums[0][y][x],&a=sums[1][y][x];
        // Plane B shows where plane A is transparent.
        const uint32_t through=a.all?((a.all-a.opaque)<<8)/a.all:256;
        for(int k=0;k<3;k++){
            // Plane B over the background colour, then plane A over that.
            const uint32_t behind=b.all?(b.colour[k]+backdrop[k]*(b.all-b.opaque))/b.all:backdrop[k];
            cell_[y][x][k]=uint8_t(a.all?(a.colour[k]+behind*(a.all-a.opaque))/a.all:behind);
            total[k]+=cell_[y][x][k];
        }
        const uint32_t energyA=a.all?a.energy/a.all:0,energyB=b.all?(b.energy/b.all*through)>>8:0,energy=energyA+energyB;   // mean of brightness^2/64
        for(int k=0;k<3;k++){
            const uint32_t brightA=a.all?a.bright[k]/a.all:0,brightB=b.all?(b.bright[k]/b.all*through)>>8:0;
            bright_[y][x][k]=energy?uint8_t(std::min<uint32_t>(255,((brightA+brightB)<<4)/energy)):cell_[y][x][k];
        }
        level[y][x]=uint32_t(isqrt(int(energy<<6)));levels+=level[y][x];
    }
    for(int k=0;k<3;k++)ambient[k]=uint8_t(total[k]/(ROWS*COLS));
    ambientLuminance_=int(levels/(ROWS*COLS));
    // What outshines the screen as a whole is a light; the rest barely counts.
    for(int y=0;y<ROWS;y++)for(int x=0;x<COLS;x++){
        const int over=std::max(0,int(level[y][x])-ambientLuminance_);
        over_[y][x]=uint16_t(over*over);
    }
}
void SceneLight::collect(int horizon){
    horizon_=horizon;lightCount_=0;
    uint32_t columns[COLS][4]{};
    for(int y=0;y<ROWS&&y*CELL+CELL/2<horizon;y++)for(int x=0;x<COLS;x++){
        const unsigned power=over_[y][x];
        if(power<24*24)continue;
        // Light near the ground spills onto it; a sign high on the wall hardly does.
        const unsigned reach=unsigned(std::max(0,160-(horizon-(y*CELL+CELL/2)))),w=(power>>4)*reach>>4;
        for(int k=0;k<3;k++)columns[x][k]+=bright_[y][x][k]*w>>8;
        columns[x][3]+=w;
    }
    // The lights: bright cells, merged two by two (every object sums over all of them).
    for(int y=0;y<ROWS;y+=2)for(int x=0;x<COLS;x+=2){
        uint32_t power=0,px=0,py=0,colour[3]{};
        for(int j=y;j<y+2&&j<ROWS&&j*CELL+CELL/2<horizon;j++)for(int i=x;i<x+2&&i<COLS;i++){
            const uint32_t w=over_[j][i];
            if(w<24*24)continue;
            power+=w;px+=w*unsigned(i*CELL+CELL/2)>>4;py+=w*unsigned(j*CELL+CELL/2)>>4;
            for(int k=0;k<3;k++)colour[k]+=w*bright_[j][i][k]>>4;
        }
        if(!power||lightCount_==MAX_LIGHTS)continue;
        const uint32_t sixteenth=std::max<uint32_t>(power>>4,1);
        lights_[lightCount_++]={int16_t(px/sixteenth),int16_t(py/sixteenth),uint16_t(std::min<uint32_t>(power,65535)),
                                {uint8_t(std::min<uint32_t>(255,colour[0]/sixteenth)),uint8_t(std::min<uint32_t>(255,colour[1]/sixteenth)),uint8_t(std::min<uint32_t>(255,colour[2]/sixteenth))}};
    }
    for(int boundary=0;boundary<=COLS;boundary++){
        // A boundary's light: its two columns', and a little of the next ones'.
        uint32_t sum[4]{};
        for(int k=-2;k<2;k++){
            const int column=boundary+k;
            if(column<0||column>=COLS)continue;
            const unsigned share=(k==-1||k==0)?3:1;
            for(int c=0;c<4;c++)sum[c]+=columns[column][c]*share;
        }
        Spill &out=spill_[boundary];
        for(int k=0;k<3;k++)out.colour[k]=sum[3]?uint8_t(std::min<uint32_t>(255,(sum[k]<<8)/sum[3])):0;
        out.strength=uint8_t(sum[3]*120/(sum[3]+24000)*profile_->spill/255);
    }
}
LightSample SceneLight::gather(int x0,int y0,int x1,int y1) const{
    // Off the screen there is what the screen's edge shows.
    const int width=COLS*CELL,height=ROWS*CELL;
    if(x1<=0){x0=0;x1=CELL;}else if(x0>=width){x0=width-CELL;x1=width;}
    if(y1<=0){y0=0;y1=CELL;}else if(y0>=height){y0=height-CELL;y1=height;}
    x0=std::max(x0,0);y0=std::max(y0,0);x1=std::min(x1,width);y1=std::min(y1,height);
    // 32-bit sums: weights are (over^2+4)/16 <= 4064, coverage <= 16 (sixteenths of a cell), 320 cells.
    uint32_t colour[3]{},weights=0,energy=0,area=0;
    for(int row=y0/CELL;row*CELL<y1;row++){
        const int rows=std::min(y1,(row+1)*CELL)-std::max(y0,row*CELL);
        for(int column=x0/CELL;column*CELL<x1;column++){
            const unsigned covered=unsigned((std::min(x1,(column+1)*CELL)-std::max(x0,column*CELL))*rows+8)>>4;
            if(!covered)continue;
            const uint8_t *c=cell_[row][column];
            const unsigned over=over_[row][column],w=((over+4)>>2)*covered;
            colour[0]+=(c[0]*w)>>6;colour[1]+=(c[1]*w)>>6;colour[2]+=(c[2]*w)>>6;
            weights+=w;energy+=(over>>2)*covered;area+=covered;
        }
    }
    LightSample sample{{255,255,255},0};
    if(!weights||!area)return sample;
    for(int k=0;k<3;k++)sample.colour[k]=uint8_t(std::min<uint32_t>(255,uint32_t((uint64_t(colour[k])<<6)/weights)));
    sample.intensity=uint8_t(std::min(255,isqrt(int(energy*4/area))*4));
    return sample;
}
void SceneLight::shade(int x0,int y0,int x1,int y1,int ground,CornerLight &light) const{
    // Every light, by its distance over the ground: it stands in the wall at the
    // horizon, the object `depth` in front of it (the ground is seen at a slant:
    // a line of the screen is about three of the ground).
    static uint16_t falloff[256];
    if(!falloff[0])for(int i=0;i<256;i++)falloff[i]=uint16_t(65536/(256+i*40));    // 256/(1+d^2/80^2), d^2 in steps of 1024
    const int middle=(x0+x1)/2,depth=std::max(0,ground-horizon_)*3+24;
    uint32_t power[2]{},place[2]{},height[2]{},colour[2][3]{};
    for(unsigned i=0;i<lightCount_;i++){
        const Light &l=lights_[i];
        const int dx=l.x-middle,up=horizon_-l.y;
        const unsigned distance=unsigned(dx*dx+depth*depth+(up*up>>2))>>10;
        const unsigned w=(l.power>>4)*falloff[std::min(distance,255u)]>>8;
        if(!w)continue;
        // Left or right of the object; a light right behind it is both.
        const unsigned right=unsigned(std::clamp(((dx+24)*683)>>7,0,256));   // 256/48 a pixel
        const unsigned share[2]={w*(256-right)>>8,w*right>>8};
        for(int g=0;g<2;g++){
            power[g]+=share[g];place[g]+=share[g]*unsigned(l.x);height[g]+=share[g]*unsigned(std::max(up,0));
            for(int k=0;k<3;k++)colour[g][k]+=share[g]*l.colour[k];
        }
    }
    const LightProfile &profile=*profile_;
    const LightSample floor=gather(x0-16,ground-8,x1+16,ground+16);
    int around[3],under[3];hue(ambient,around);hue(floor.colour,under);
    // Across the width, as a cylinder's surface takes light from lights 60 degrees
    // to each side (of 255), plus a fifth of all the light as fill from the scene.
    static constexpr uint8_t facing[CornerLight::COLUMNS]={222,247,128,0,0};
    const uint32_t exposure=profile.exposure;
    int edge[2]{};
    for(int c=0;c<CornerLight::COLUMNS;c++){
        const uint32_t w[2]={facing[c],facing[CornerLight::COLUMNS-1-c]};
        const uint32_t reaching=(power[0]*w[0]+power[1]*w[1])/255+(power[0]+power[1])/5;
        const int lit=int(reaching*255/(reaching+exposure));
        int from[3];
        const uint32_t weight=std::max<uint32_t>(1,(power[0]*(w[0]+51)+power[1]*(w[1]+51))>>8);
        const uint8_t mixed[3]={uint8_t(std::min<uint32_t>(255,((colour[0][0]>>8)*(w[0]+51)+(colour[1][0]>>8)*(w[1]+51))/weight)),
                                uint8_t(std::min<uint32_t>(255,((colour[0][1]>>8)*(w[0]+51)+(colour[1][1]>>8)*(w[1]+51))/weight)),
                                uint8_t(std::min<uint32_t>(255,((colour[0][2]>>8)*(w[0]+51)+(colour[1][2]>>8)*(w[1]+51))/weight))};
        hue(power[0]+power[1]?mixed:ambient,from);
        if(c==0)edge[0]=lit;
        if(c==CornerLight::COLUMNS-1)edge[1]=lit;
        for(int k=0;k<3;k++){
            const int scale=towards(around[k],profile.ambientTint)*towards(profile.sky[k],profile.skyTint)/255
                *towards(from[k],profile.lightTint*lit/255)/255*(profile.unlit+(255-profile.unlit)*lit/255)/255;
            const int added=from[k]*lit/255*ADDED/255;
            light.column[c][0].scale[k]=uint8_t(scale);
            light.column[c][0].offset[k]=uint8_t(added);
            // The feet: a little darker, in the ground's colour, with half the added light.
            light.column[c][1].scale[k]=uint8_t(scale*FEET/255*towards(under[k],GROUND_TINT)/255);
            light.column[c][1].offset[k]=uint8_t(added/2);
        }
    }
    // The edge towards a side's lights catches them: more the more that side outshines the other.
    for(int g=0;g<2;g++){
        RimLight &rim=light.rim[g];rim=RimLight{};
        if(!power[g])continue;
        const int share=int(uint64_t(power[g])*255/(power[0]+power[1]));
        rim.alpha=uint8_t(edge[g]*profile.rim/255*(64+share*191/255)/255);
        if(rim.alpha<28){rim.alpha=0;continue;}
        int from[3];
        const uint8_t mixed[3]={uint8_t(colour[g][0]/power[g]),uint8_t(colour[g][1]/power[g]),uint8_t(colour[g][2]/power[g])};
        hue(mixed,from);
        for(int k=0;k<3;k++)rim.colour[k]=uint8_t(128+from[k]/2);     // the light's colour, towards white
    }
    // A shadow away from each group's centre of power: per unit of the object's
    // height it runs (object - light)/(the light's height) over the ground. The
    // round's sky casts one of its own, and has its share of the darkness.
    const int total=int(std::min<uint32_t>(power[0]+power[1],0x7FFFFF));
    const int reach=int(uint64_t(total)*255/(uint64_t(total)+exposure));
    const bool lights=total>=int(exposure/16);
    const int skyShare=lights?profile.skyShare:255,alpha=profile.shadowAlpha*(150+reach*105/255)/255;
    for(int g=0;g<2;g++){
        CastShadow &shadow=light.shadow[g];shadow=CastShadow{};
        if(!power[g]||!lights)continue;
        const int x=int(place[g]/power[g]),high=int(height[g]/power[g])+40;
        shadow.lean=int8_t(std::clamp((middle-x)*64/high,-72,72));
        shadow.length=uint8_t(std::clamp(depth*64/(3*high),14,44));
        shadow.alpha=uint8_t(alpha*(255-skyShare)/255*int(std::min<uint32_t>(power[g],0x7FFFFF))/total);
        if(shadow.alpha<10)shadow.alpha=0;
    }
    light.shadow[2]={profile.skyLean,profile.skyLength,uint8_t(profile.shadowAlpha*2/3*skyShare/255)};
    if(light.shadow[2].alpha<10)light.shadow[2].alpha=0;
}
void SceneLight::glow(const LightEmitter &e,int x0,int y0,int x1,int y1,CornerLight &light){
    for(int c=0;c<CornerLight::COLUMNS;c++)for(int row=0;row<2;row++){
        const int dx=x0+(x1-x0)*c/(CornerLight::COLUMNS-1)-e.x,dy=(row?y1:y0)-e.y,distance=isqrt(dx*dx+dy*dy);
        if(distance>=e.radius)continue;
        const int near=(e.radius-distance)*255/e.radius,amount=near*near/255*e.strength/255;
        ArtTint &tint=light.column[c][row];
        for(int k=0;k<3;k++)tint.offset[k]=uint8_t(std::min(255,tint.offset[k]+e.colour[k]*amount/255));
    }
}
void SceneLight::apply(const ArtTint &tint,CornerLight &light){
    if(tint.identity())return;
    for(auto &column:light.column)for(auto &c:column)for(int k=0;k<3;k++){
        c.offset[k]=uint8_t(std::min(255,c.offset[k]*tint.scale[k]/255+tint.offset[k]));
        c.scale[k]=uint8_t(c.scale[k]*tint.scale[k]/255);
    }
}
}
