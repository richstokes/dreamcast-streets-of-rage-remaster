#pragma once
#include "VDPState.hpp"
#include <array>
#include <cstdint>

namespace sor {
// Title lettering, in the title screen's 320x224 space.
// Keep this in the presentation layer: the ROM, VDP and gameplay RAM are untouched.
class TitleCaption {
public:
    static constexpr int X=14,Y=110,WIDTH=84,HEIGHT=13;
    inline static constexpr char CREDIT[]="REMASTERED 2026 - RICH STOKES";
    static constexpr int CREDIT_WIDTH=[] {
        int width=-1;
        for(char c:CREDIT)if(c)width+=c==' '?4:c=='I'?2:6;
        return width;
    }(),CREDIT_HEIGHT=7;
    struct Region {int x,y,width,height,textureWidth,textureHeight;};
    inline static constexpr std::array<Region,2> regions{{
        {X,Y,WIDTH,HEIGHT,128,16},
        {(320-CREDIT_WIDTH)/2,181,CREDIT_WIDTH,CREDIT_HEIGHT,256,16}
    }};
    static constexpr int MAX_TEXTURE_PIXELS=256*16;
    uint8_t brightness=0;

    TitleCaption()=default;
    TitleCaption(const VDPState &state,uint16_t gameMode){
        // $FFFF00 == $000A is the settled title, after the moving intro logo.
        if(gameMode!=0x000a || !state.displayEnabled()
            || state.activeWidth()!=320 || state.activeHeight()!=224)return;
        font_=state.vram_;
        creditColor_=state.cram_[33]; // Palette 2, ink 1: original copyright white.
        // Follow palette fades/blanking instead of leaving a bright caption behind.
        for(auto color:state.cram_)for(int shift:{1,5,9}){
            auto channel=uint8_t((color>>shift)&7);
            if(channel>brightness)brightness=channel;
        }
    }

    uint16_t textureKey()const{return uint16_t((brightness<<12)|creditColor_);}

    template<class PutPixel> void draw(PutPixel put)const{
        for(unsigned layer=0;layer<regions.size();layer++)drawLayer(layer,put);
    }

    template<class PutPixel> void drawLayer(unsigned layer,PutPixel put)const{
        if(!brightness)return;
        if(layer){drawCredit(put);return;}
        const auto &region=regions[layer];
        for(int y=0;y<region.height;y++)for(int x=0;x<region.width;x++){
            auto ink=bitmap[y*region.width+x];
            if(!ink)continue;
            const auto &color=colors[ink-1];
            put(region.x+x,region.y+y,color[0]*brightness/7,color[1]*brightness/7,color[2]*brightness/7);
        }
    }

private:
    const uint8_t *font_=nullptr;
    uint16_t creditColor_=0;
    // Six-pixel-wide, nine-pixel-high caps; two-pixel tracking and a stepped
    // italic lean echo the angular logo.
    inline static constexpr uint8_t letters[10][9]={
        {0x3e,0x33,0x33,0x33,0x3e,0x36,0x33,0x33,0x33}, // R
        {0x3f,0x30,0x30,0x30,0x3e,0x30,0x30,0x30,0x3f}, // E
        {0x33,0x3f,0x3f,0x3f,0x33,0x33,0x33,0x33,0x33}, // M
        {0x1e,0x33,0x33,0x33,0x3f,0x33,0x33,0x33,0x33}, // A
        {0x1f,0x30,0x30,0x30,0x1e,0x03,0x03,0x03,0x3e}, // S
        {0x3f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c}, // T
        {0x3f,0x30,0x30,0x30,0x3e,0x30,0x30,0x30,0x3f}, // E
        {0x3e,0x33,0x33,0x33,0x3e,0x36,0x33,0x33,0x33}, // R
        {0x3f,0x30,0x30,0x30,0x3e,0x30,0x30,0x30,0x3f}, // E
        {0x3e,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x3e}, // D
    };
    // Rows use the Mega Drive's 3-bit RGB.
    inline static constexpr uint8_t colors[5][3]={
        {0,0,0}, {3,1,0}, {5,3,0}, {7,5,0}, {7,7,3}
    };
    inline static constexpr auto bitmap=[] {
        std::array<uint8_t,WIDTH*HEIGHT> pixels{};
        // Outline and a two-pixel drop shadow separate the letters from the city.
        for(int pass=0;pass<2;pass++)for(int n=0;n<10;n++)for(int y=0;y<9;y++)for(int x=0;x<6;x++){
            if(!(letters[n][y]&(0x20>>x)))continue;
            const int px=1+n*8+x+(8-y)/3,py=1+y;
            if(!pass){
                for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)pixels[(py+dy)*WIDTH+px+dx]=1;
                pixels[(py+2)*WIDTH+px+2]=1;
            }else pixels[py*WIDTH+px]=y<2?5:y<5?4:y<8?3:2;
        }
        return pixels;
    }();

    // Reuse the title's resident copyright/prompt lettering, including its
    // narrow I and heavier left strokes.
    struct Glyph {char character;uint16_t tile;uint8_t x,width;};
    inline static constexpr Glyph creditFont[]={
        {'R',0x31,6,5}, {'E',0x31,12,5}, {'M',0x45,0,5},
        {'A',0x3d,55,5}, {'S',0x31,18,5}, {'T',0x31,37,5},
        {'I',0x45,18,1}, {'C',0x45,20,5}, {'H',0x4d,45,5},
        {'K',0x4d,27,5}, {'0',0x4d,18,5}, {'O',0x4d,18,5}
    };
    // The precomposed original credits lack D, 2, 6 and a dash. These few
    // hand-drawn additions use the same seven rows and two-pixel left stem.
    struct ExtraGlyph {char character;uint8_t rows[7];};
    inline static constexpr ExtraGlyph extraCreditFont[]={
        {'D',{30,25,25,25,25,25,30}}, {'2',{14,25,3,6,12,24,31}},
        {'6',{14,25,24,30,25,25,14}}, {'-',{0,0,0,31,0,0,0}}
    };
    template<class PutPixel> void drawCredit(PutPixel put)const{
        int pen=regions[1].x;
        const unsigned r=(creditColor_>>1)&7,g=(creditColor_>>5)&7,b=(creditColor_>>9)&7;
        for(char c:CREDIT){
            if(!c)break;
            for(const auto &glyph:creditFont)if(glyph.character==c){
                for(int y=0;y<7;y++)for(int x=0;x<glyph.width;x++){
                    const int column=glyph.x+x;
                    const auto packed=font_[(glyph.tile+column/8)*32+(y+1)*4+(column%8)/2];
                    if((packed>>(column&1?0:4))&15)put(pen+x,regions[1].y+y,r,g,b);
                }
            }
            for(const auto &glyph:extraCreditFont)if(glyph.character==c)
                for(int y=0;y<7;y++)for(int x=0;x<5;x++)
                    if(glyph.rows[y]&(0x10>>x))put(pen+x,regions[1].y+y,r,g,b);
            pen+=c==' '?4:c=='I'?2:6;
        }
    }
};
}
