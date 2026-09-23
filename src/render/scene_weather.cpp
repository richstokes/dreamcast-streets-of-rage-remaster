#include "scene_weather.hpp"
#include "scene_light.hpp"
#include <algorithm>
namespace sor {
namespace {
constexpr int T=WEATHER_TEXTURE;
uint32_t hash(uint32_t x,uint32_t y,uint32_t seed){
    uint32_t h=x*0x8DA6B343u^y*0xD8163841u^seed*0xCB1AB31Fu;
    h^=h>>13;h*=0x5BD1E995u;h^=h>>15;
    return h;
}
int clamp255(int v){return std::clamp(v,0,255);}
// Value noise on a lattice of `cells` per texture, bilinear with a smooth step: tileable.
int lattice(int x,int y,int cells,uint32_t seed){
    const int size=T/cells,cx=x/size,cy=y/size,fx=(x%size)*256/size,fy=(y%size)*256/size;
    const int sx=fx*fx*(768-2*fx)>>16,sy=fy*fy*(768-2*fy)>>16;   // 3t^2 - 2t^3, of 256
    const auto at=[&](int i,int j){return int(hash(unsigned(i%cells),unsigned(j%cells),seed)&255);};
    const int top=at(cx,cy)*(256-sx)+at(cx+1,cy)*sx,bottom=at(cx,cy+1)*(256-sx)+at(cx+1,cy+1)*sx;
    return (top*(256-sy)+bottom*sy)>>16;
}
}
const WeatherProfile &weather_profile(unsigned round){
    // Rain falls on the street (1), the bridge (4) and the lift (7); the street and
    // the bridge are wet and stormy. Haze and mist thicken the outdoors; the
    // factory (6) is full of steam. The ship (5) and the headquarters (8) are rooms.
    //                       rain wet  fog  fog colour        mist shafts lightning
    static const WeatherProfile profiles[9]={
        /* default */        { 0,   0,   0,   {0,0,0},          0,   0,    0},
        /* 1 street */       { 190, 190, 40,  {70,80,110},      0,   90,   150},
        /* 2 inner city */   { 0,   70,  90,  {60,70,100},      70,  110,  0},
        /* 3 beach */        { 0,   0,   70,  {90,110,150},     120, 40,   0},
        /* 4 bridge */       { 120, 130, 110, {70,85,120},      90,  120,  90},
        /* 5 ship */         { 0,   0,   0,   {0,0,0},          0,   0,    0},
        /* 6 factory */      { 0,   0,   50,  {160,150,140},    100, 130,  0},
        /* 7 lift */         { 160, 0,   80,  {60,70,100},      40,  60,   140},
        /* 8 headquarters */ { 0,   0,   0,   {0,0,0},          0,   0,    0},
    };
    return profiles[round<=8?round:0];
}
const uint8_t *weather_texture(WeatherTexture which){
    static uint8_t streak[T*T],noise[T*T];
    static bool made=false;
    if(!made){
        made=true;
        // Rain: thin streaks, brighter towards their head, wrapping top to bottom.
        for(int k=0;k<11;k++){
            const int column=int(hash(k,1,7)%T),top=int(hash(k,2,7)%T),length=26+int(hash(k,3,7)%30),bright=150+int(hash(k,4,7)%105);
            for(int y=0;y<length;y++){
                const int row=(top+y)%T,tail=std::min(y+4,length-y)*255/(length/2+4);   // in at the tail, brightest towards the head
                streak[row*T+column]=uint8_t(std::max<int>(streak[row*T+column],bright*std::min(255,tail)/255));
            }
        }
        // Mist: three octaves of value noise, shaped into soft patches with holes.
        for(int y=0;y<T;y++)for(int x=0;x<T;x++){
            const int n=(lattice(x,y,4,11)*4+lattice(x,y,8,13)*2+lattice(x,y,16,17))/7;
            noise[y*T+x]=uint8_t(clamp255((n-88)*255/100));
        }
    }
    return which==WeatherTexture::STREAK?streak:noise;
}
int weather_sample(WeatherTexture which,int u16,int v16){
    if(which==WeatherTexture::NONE)return 255;
    const uint8_t *t=weather_texture(which);
    const int u=u16>>4,v=v16>>4,fu=u16&15,fv=v16&15;
    const auto at=[&](int x,int y){return int(t[(y&(T-1))*T+(x&(T-1))]);};
    const int top=at(u,v)*(16-fu)+at(u+1,v)*fu,bottom=at(u,v+1)*(16-fu)+at(u+1,v+1)*fu;
    return (top*(16-fv)+bottom*fv)>>8;
}
void Weather::strike(int strength){
    flash_=uint8_t(strength);hold_=2;
    lean_=int8_t(-40+int(random()%81));
}
void Weather::advance(unsigned ticks,const WeatherProfile &profile){
    for(;ticks;ticks--){
        time_++;
        if(!profile.lightning){flash_=0;second_=0;continue;}
        // A flash holds, then dies away; a strike now and then, sometimes twice.
        if(flash_){
            if(hold_)hold_--;
            else flash_=uint8_t(flash_>=12?flash_*3/4:0);
        }
        if(second_&&!--second_)strike(170);
        if(countdown_)countdown_--;
        else{
            if(time_>1)strike(255);
            countdown_=uint16_t(std::min<uint32_t>(65535,(500u+random()%1400u)*255u/profile.lightning));
            second_=uint8_t(random()%3==0?5+random()%6:0);
        }
    }
}
int weather_fog(const WeatherProfile &p,int y,int wallLine,bool farPlane){
    if(!p.fog)return 0;
    // Full 80 lines above the wall line, none 40 lines below it (the ground there is near).
    const int t=std::clamp((wallLine+40-y)*255/120,0,255),f=p.fog*t/255;
    return farPlane?f:f*5/8;
}
namespace {
WeatherQuad &sheet(WeatherQuad *out,size_t &n,WeatherTexture texture,WeatherQuad::Depth depth,bool additive,
                   int x0,int y0,int x1,int y1,int shear,int texelHalfPixels16,int u,int v,const uint8_t colour[3],int alphaTop,int alphaBottom){
    // A sheet of texture over [x0,x1) x [y0,y1) in half pixels, its top moved
    // `shear` to the left; texelHalfPixels16: half pixels per texel, in 1/16.
    WeatherQuad &q=out[n++];
    q.texture=texture;q.depth=depth;q.additive=additive;
    q.x[0]=int16_t(x0-shear);q.x[1]=int16_t(x1-shear);q.x[2]=int16_t(x0);q.x[3]=int16_t(x1);
    q.y[0]=q.y[1]=int16_t(y0);q.y[2]=q.y[3]=int16_t(y1);
    const int uSpan=(x1-x0)*16/texelHalfPixels16,vSpan=(y1-y0)*16/texelHalfPixels16;
    q.u[0]=q.u[2]=int16_t(u);q.u[1]=q.u[3]=int16_t(u+uSpan);
    q.v[0]=q.v[1]=int16_t(v);q.v[2]=q.v[3]=int16_t(v+vSpan);
    std::copy(colour,colour+3,q.colour);
    q.alpha[0]=q.alpha[1]=uint8_t(alphaTop);q.alpha[2]=q.alpha[3]=uint8_t(alphaBottom);
    return q;
}
}
size_t weather_quads(const WeatherProfile &p,const Weather &w,int camera,int width,int height,int wallLine,
                     const Light *lights,unsigned lightCount,WeatherQuad *out){
    size_t n=0;
    const int W=width*2,H=height*2,top=HUD_LINES*2,time=int(w.time()),camera2=camera*2;
    const auto wrap=[](int v){return ((v%T)+T)%T;};
    if(p.rain){
        // Near: in front of everything, big streaks, fast; far: behind the
        // characters, smaller, slower, moving with the world a little.
        static const uint8_t drop[3]={190,205,225};
        sheet(out,n,WeatherTexture::STREAK,WeatherQuad::FRONT,true,-64,top,W+64,H,32,32,wrap(time/3),T-wrap(time*7),drop,p.rain*120/255,p.rain*120/255);
        sheet(out,n,WeatherTexture::STREAK,WeatherQuad::GROUND,true,-64,top,W+64,H,18,20,wrap(camera2/2+time/5),T-wrap(time*5),drop,p.rain*50/255,p.rain*50/255);
    }
    if(p.mist){
        // Pale: the fog's colour towards white.
        const uint8_t pale[3]={uint8_t(p.fogColour[0]+(255-p.fogColour[0])*11/20),uint8_t(p.fogColour[1]+(255-p.fogColour[1])*11/20),uint8_t(p.fogColour[2]+(255-p.fogColour[2])*11/20)};
        const int rise=std::max(top,(wallLine-20)*2),peak=(wallLine+16)*2,floor=std::min(H,(wallLine+70)*2);
        // Two layers at the ground, at different scales and drifts, so that the pattern does not show.
        if(peak>rise&&floor>peak){
            const int a=p.mist*150/255,b=p.mist*110/255;
            sheet(out,n,WeatherTexture::NOISE,WeatherQuad::GROUND,false,0,rise,W,peak,0,32,wrap(time/2+camera2*3/8),wrap(time/6),pale,0,a);
            sheet(out,n,WeatherTexture::NOISE,WeatherQuad::GROUND,false,0,peak,W,floor,0,32,wrap(time/2+camera2*3/8),wrap(time/6)+(peak-rise)/2,pale,a,0);
            sheet(out,n,WeatherTexture::NOISE,WeatherQuad::GROUND,false,0,rise,W,peak,0,48,wrap(-time/3+camera2*5/8),wrap(-time/9),pale,0,b);
            sheet(out,n,WeatherTexture::NOISE,WeatherQuad::GROUND,false,0,peak,W,floor,0,48,wrap(-time/3+camera2*5/8),wrap(-time/9)+(peak-rise)/3,pale,b,0);
        }
        // A faint veil in front of everything from the wall line down, thicker low
        // down (not over the whole playfield: a full-screen translucent layer is
        // fill the PowerVR pays for every frame).
        const int veil=std::max(top,(wallLine-32)*2);
        if(H>veil)sheet(out,n,WeatherTexture::NOISE,WeatherQuad::FRONT,false,0,veil,W,H,0,64,wrap(time/3),wrap(time/8),pale,0,p.mist*46/255);
    }
    // The wall's lights: smeared down the wet ground below them, and shafts
    // through the fog from the light to the ground line, fanning out.
    unsigned smears=0,shafts=0;
    for(unsigned i=0;i<lightCount&&n+3<=MAX_WEATHER_QUADS;i++){
        const Light &l=lights[i];
        if(l.low||l.power<4000)continue;
        const int norm=std::min(255,int(l.power/128)),cx=l.x*2;
        if(p.wet&&smears<12){
            // Two halves, full in the middle and nothing at the sides, widening
            // downwards and fading; the noise breaks it into wet patches, and
            // the world's scroll keeps the patches with the ground.
            smears++;
            const int w=16+norm/6,h=(36+norm/6)*2,y1=std::min(H,wallLine*2+h),a=p.wet*norm/255*170/255;
            const int u=wrap(cx/3+camera2/3),v=wrap(wallLine);
            for(int side=0;side<2;side++){
                WeatherQuad &q=out[n++];
                q.texture=WeatherTexture::NOISE;q.depth=WeatherQuad::GROUND;q.additive=true;
                const int x0=side?cx:cx-w/2,x1=side?cx+w/2:cx,x2=side?cx:cx-w*3/4,x3=side?cx+w*3/4:cx;
                q.x[0]=int16_t(x0);q.x[1]=int16_t(x1);q.x[2]=int16_t(x2);q.x[3]=int16_t(x3);
                q.y[0]=q.y[1]=int16_t(wallLine*2);q.y[2]=q.y[3]=int16_t(y1);
                q.u[0]=q.u[2]=int16_t(u+side*10);q.u[1]=q.u[3]=int16_t(u+10+side*10);
                q.v[0]=q.v[1]=int16_t(v);q.v[2]=q.v[3]=int16_t(v+(y1-wallLine*2)/6);
                std::copy(l.colour,l.colour+3,q.colour);
                q.alpha[0]=uint8_t(side?a:0);q.alpha[1]=uint8_t(side?0:a);q.alpha[2]=q.alpha[3]=0;
            }
        }
        if(p.shafts&&p.fog&&shafts<12&&l.power>=2500&&l.y<wallLine-8){
            shafts++;
            const int w0=(10+norm/12)*2,w1=w0*3;
            WeatherQuad &q=out[n++];
            q.texture=WeatherTexture::NONE;q.depth=WeatherQuad::GROUND;q.additive=true;
            q.x[0]=int16_t(cx-w0/2);q.x[1]=int16_t(cx+w0/2);q.x[2]=int16_t(cx-w1/2);q.x[3]=int16_t(cx+w1/2);
            q.y[0]=q.y[1]=int16_t(l.y*2);q.y[2]=q.y[3]=int16_t((wallLine+8)*2);
            for(int k=0;k<4;k++){q.u[k]=0;q.v[k]=0;}
            for(int k=0;k<3;k++)q.colour[k]=uint8_t((l.colour[k]+p.fogColour[k])/2);
            const int a=p.shafts*norm/255*p.fog/255*110/255;
            q.alpha[0]=q.alpha[1]=uint8_t(a);q.alpha[2]=q.alpha[3]=uint8_t(a/4);
        }
    }
    if(w.flash()&&n<MAX_WEATHER_QUADS){
        static const uint8_t bolt[3]={225,232,255};
        sheet(out,n,WeatherTexture::NONE,WeatherQuad::FRONT,true,0,top,W,H,0,16,0,0,bolt,w.flash()*90/255,w.flash()*90/255);
    }
    return n;
}
}
