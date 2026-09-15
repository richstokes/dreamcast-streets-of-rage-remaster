#pragma once
#include "VDPState.hpp"
#include <cstring>
class Framebuffer {
public:
    static constexpr int WIDTH=320,HEIGHT=480,BPP=3,PITCH=WIDTH*BPP,SIZE=PITCH*HEIGHT;
    m_byte pixels_[SIZE]{};
    void *getRawPointer(){return pixels_;}
    const void *getRawPointer()const{return pixels_;}
    void clear(){std::memset(pixels_,0,SIZE);}
    void setPixel(int x,int y,m_byte b,m_byte g,m_byte r){if(x<0||y<0||x>=WIDTH||y>=HEIGHT)return;auto p=pixels_+y*PITCH+x*3;p[0]=b;p[1]=g;p[2]=r;}
};
