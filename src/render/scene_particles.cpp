#include "scene_particles.hpp"
#include <algorithm>
namespace sor {
void Particles::spawn(Kind kind,int screenX,int screenY,int camera,int vx,int vy,unsigned life){
    // Full: the oldest makes room (the nearest its end).
    size_t slot=count_;
    if(count_==MAX){
        slot=0;
        for(size_t i=1;i<MAX;i++)if(particles_[i].life<particles_[slot].life)slot=i;
    }else count_++;
    particles_[slot]={(screenX+camera)*16,screenY*16,int16_t(vx),int16_t(vy),uint8_t(life),uint8_t(life),kind};
}
void Particles::fire(int x,int y,int camera){
    // An ember now and then from somewhere in the flame, rising and drifting.
    if(random()%3)return;
    spawn(EMBER,x+range(-10,10),y-range(6,34),camera,range(-6,6),-range(12,26),unsigned(range(26,50)));
}
void Particles::fireball(int x,int y,int camera){
    spawn(EMBER,x+range(-4,4),y-range(4,12),camera,range(-8,8),-range(2,10),unsigned(range(10,20)));
    if(random()%2)spawn(SPARK,x+range(-3,3),y-range(2,10),camera,range(-20,20),-range(4,24),unsigned(range(8,14)));
}
void Particles::rocket(int x,int y,int camera){
    spawn(SMOKE,x+range(-2,2),y-range(2,8),camera,range(-3,3),-range(1,5),unsigned(range(24,36)));
    if(random()%2)spawn(SPARK,x+range(-2,2),y-range(2,8),camera,range(-14,14),range(-6,14),unsigned(range(6,12)));
}
void Particles::burst(int x,int y,int camera){
    for(int i=0;i<7;i++)spawn(SPARK,x,y-8,camera,range(-38,38),-range(4,40),unsigned(range(8,16)));
}
void Particles::dust(int x,int y,int camera){
    for(int i=0;i<5;i++)spawn(DUST,x+range(-6,6),y-range(0,3),camera,(i&1?1:-1)*range(6,22),-range(1,5),unsigned(range(12,20)));
}
void Particles::debris(int x,int y,int camera){
    for(int i=0;i<9;i++)spawn(DEBRIS,x+range(-8,8),y-range(0,24),camera,range(-34,34),-range(16,52),unsigned(range(18,30)));
    dust(x,y+16,camera);
}
void Particles::splash(int x,int y,int camera){spawn(SPLASH,x,y,camera,range(-4,4),-range(3,8),unsigned(range(5,8)));}
void Particles::advance(unsigned ticks){
    for(;ticks;ticks--){
        for(size_t i=0;i<count_;){
            Particle &p=particles_[i];
            if(!--p.life){p=particles_[--count_];continue;}
            p.x+=p.vx;p.y+=p.vy;
            switch(p.kind){
            case EMBER:p.vx=int16_t(p.vx+int(random()%5)-2);p.vy=int16_t(p.vy*15/16);break;   // drifts, slows
            case SPARK:p.vy=int16_t(p.vy+3);p.vx=int16_t(p.vx*15/16);break;                     // falls
            case SMOKE:p.vx=int16_t(p.vx*7/8);break;
            case DUST:p.vx=int16_t(p.vx*13/16);p.vy=int16_t(p.vy*7/8);break;                  // spreads, settles
            case DEBRIS:p.vy=int16_t(p.vy+4);break;                                             // thrown, falls
            case SPLASH:p.vy=int16_t(p.vy+2);break;
            }
            i++;
        }
    }
}
size_t Particles::draw(int camera,int width,int height,ParticleDraw *out) const{
    size_t n=0;
    for(size_t i=0;i<count_;i++){
        const Particle &p=particles_[i];
        const int x=p.x/8-camera*2,y=p.y/8;                        // half pixels
        if(x<-16||y<HUD_LINES*2||x>width*2+16||y>height*2+16)continue;
        const int left=p.life*255/p.span;                           // 255 new, 0 gone
        ParticleDraw d{int16_t(x),int16_t(y),2,{255,255,255},255,true};
        switch(p.kind){
        case EMBER:   // yellow, then orange, then red, fading
            d.radius=uint8_t(left>128?6:4);
            d.colour[1]=uint8_t(std::min(255,70+left*3/4));d.colour[2]=uint8_t(left>200?(left-200)*3:0);
            d.alpha=uint8_t(std::min(255,left*2));break;
        case SPARK:   // white hot, then yellow
            d.radius=uint8_t(left>100?5:3);
            d.colour[2]=uint8_t(left/2);d.colour[1]=uint8_t(160+left*95/255);
            d.alpha=uint8_t(std::min(255,left*3));break;
        case SMOKE:   // grey, growing and thinning
            d.additive=false;d.radius=uint8_t(5+(255-left)/24);
            d.colour[0]=d.colour[1]=d.colour[2]=uint8_t(120+left/4);
            d.alpha=uint8_t(left*110/255);break;
        case DUST:    // pale, low, brief
            d.additive=false;d.radius=uint8_t(4+(255-left)/40);
            d.colour[0]=190;d.colour[1]=185;d.colour[2]=170;
            d.alpha=uint8_t(left*90/255);break;
        case DEBRIS:  // dark pieces, whole until they go
            d.additive=false;d.radius=3;
            d.colour[0]=110;d.colour[1]=72;d.colour[2]=40;
            d.alpha=uint8_t(std::min(255,left*4));break;
        case SPLASH:  // a glint
            d.radius=2;d.colour[0]=170;d.colour[1]=200;
            d.alpha=uint8_t(left*150/255);break;
        }
        out[n++]=d;
    }
    return n;
}
}
