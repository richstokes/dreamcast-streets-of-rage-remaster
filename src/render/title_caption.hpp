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
    static constexpr int TEXTURE_WIDTH=128,TEXTURE_HEIGHT=16;
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
        if(!brightness)return;
        for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
            auto ink=bitmap[y*WIDTH+x];
            if(!ink)continue;
            const auto &color=colors[ink-1];
            put(X+x,Y+y,color[0]*brightness/7,color[1]*brightness/7,color[2]*brightness/7);
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
};
}
