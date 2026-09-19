#pragma once
#include "VDPState.hpp"
#include <array>
#include <cstdint>

namespace sor {
// Original, hand-drawn pixel lettering, in the title screen's 320x224 space.
// Keep this in the presentation layer: the ROM, VDP and gameplay RAM are untouched.
class TitleCaption {
public:
    static constexpr int X=14,Y=110,WIDTH=84,HEIGHT=13;
    inline static constexpr char CREDIT[]="Remastered 2026 - Rich Stokes";
    static constexpr int CREDIT_WIDTH=(sizeof(CREDIT)-1)*6+1,CREDIT_HEIGHT=9;
    struct Region {int x,y,width,height,textureWidth,textureHeight;};
    inline static constexpr std::array<Region,2> regions{{
        {X,Y,WIDTH,HEIGHT,128,16},
        {(320-CREDIT_WIDTH)/2,180,CREDIT_WIDTH,CREDIT_HEIGHT,256,16}
    }};
    static constexpr int MAX_TEXTURE_PIXELS=256*16;
    uint8_t brightness=0;

    TitleCaption()=default;
    TitleCaption(const VDPState &state,uint16_t gameMode){
        // $FFFF00 == $000A is the settled title, after the moving intro logo.
        if(gameMode!=0x000a || !state.displayEnabled()
            || state.activeWidth()!=320 || state.activeHeight()!=224)return;
        // Follow palette fades/blanking instead of leaving a bright caption behind.
        for(auto color:state.cram_)for(int shift:{1,5,9}){
            auto channel=uint8_t((color>>shift)&7);
            if(channel>brightness)brightness=channel;
        }
    }

    template<class PutPixel> void draw(PutPixel put)const{
        for(unsigned layer=0;layer<regions.size();layer++)drawLayer(layer,put);
    }

    template<class PutPixel> void drawLayer(unsigned layer,PutPixel put)const{
        if(!brightness)return;
        const auto &region=regions[layer];
        const auto *pixels=layer?creditBitmap.data():bitmap.data();
        for(int y=0;y<region.height;y++)for(int x=0;x<region.width;x++){
            auto ink=pixels[y*region.width+x];
            if(!ink)continue;
            const auto &color=colors[ink-1];
            put(region.x+x,region.y+y,color[0]*brightness/7,color[1]*brightness/7,color[2]*brightness/7);
        }
    }

private:
    // Six-pixel-wide, nine-pixel-high caps; two-pixel tracking and a stepped
    // italic lean echo the angular logo. Rows use the Mega Drive's 3-bit RGB.
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

    // A compact mixed-case 5x7 credit, centered above the original copyrights.
    struct Glyph {char character;uint8_t rows[7];};
    inline static constexpr Glyph creditFont[]={
        {'R',{30,17,17,30,20,18,17}}, {'e',{0,0,14,17,31,16,14}},
        {'m',{0,0,26,21,21,21,21}}, {'a',{0,0,14,1,15,17,15}},
        {'s',{0,0,15,16,14,1,30}}, {'t',{4,4,31,4,4,5,2}},
        {'r',{0,0,22,25,16,16,16}}, {'d',{1,1,15,17,17,17,15}},
        {'2',{14,17,1,2,4,8,31}}, {'0',{14,17,19,21,25,17,14}},
        {'6',{6,8,16,30,17,17,14}}, {'-',{0,0,0,31,0,0,0}},
        {'i',{4,0,12,4,4,4,14}}, {'c',{0,0,14,17,16,17,14}},
        {'h',{16,16,22,25,17,17,17}}, {'S',{15,16,16,14,1,1,30}},
        {'o',{0,0,14,17,17,17,14}}, {'k',{16,16,18,20,24,20,18}}
    };
    inline static constexpr auto creditBitmap=[] {
        std::array<uint8_t,CREDIT_WIDTH*CREDIT_HEIGHT> pixels{};
        for(int pass=0;pass<2;pass++)for(unsigned n=0;n<sizeof(CREDIT)-1;n++)for(const auto &glyph:creditFont){
            if(glyph.character!=CREDIT[n])continue;
            for(int y=0;y<7;y++)for(int x=0;x<5;x++){
                if(!(glyph.rows[y]&(0x10>>x)))continue;
                const int px=1+n*6+x,py=1+y;
                if(!pass){
                    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)pixels[(py+dy)*CREDIT_WIDTH+px+dx]=1;
                }else pixels[py*CREDIT_WIDTH+px]=5;
            }
        }
        return pixels;
    }();
};
}
