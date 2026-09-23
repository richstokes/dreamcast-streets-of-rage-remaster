#include "SoR.hpp"
#include "platform.hpp"
#include "replay.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include "vdp_scene.hpp"
#include "extract_frames.hpp"
#include "cheats.hpp"
#include "art_catalog.hpp"
#include "audio_core.hpp"
#include <vector>

namespace {
struct ReplayFinished {};
FILE *trace=nullptr;
std::string capturePath;
std::unique_ptr<sor::VdpScene> scene;
VDPState *sceneState=nullptr;
unsigned sceneFrames=0;
sor::TitleCaption sceneTitle;
// Enhanced-rendering preview (SOR_ENHANCED_CAPTURE=dir:first:last:step, art
// from SOR_ART): original at 2x on the left, enhanced on the right.
// SOR_SMOOTH=1: smooth animation (in-between poses; needs a step of 1).
// SOR_LIGHTING=0: no dynamic lighting (shadows, light from the backdrop and from fire); on by default.
// SOR_WEATHER=1: the round's weather (rain, wet ground, haze, mist, lightning; with lighting);
// 2: the same, and lightning strikes two captured frames in (a step of 1 shows it die away).
unsigned gameRound=0;   // 1-8, from the runtime
bool gamePlaying=false; // in a round (not the title, menus, cutscenes or the ending): gates lighting and weather
struct EnhancedCapture {
    std::string directory;unsigned first=0,last=0,step=1,frame=0;bool pending=false;
    std::vector<uint8_t> package;sor::ArtCatalog art;
    std::unique_ptr<sor::VdpScene> scene;std::vector<uint16_t> image;int width=0,height=0;
} capture;
void capture_setup(){
    static bool done=false;if(done)return;done=true;
    if(const char *path=std::getenv("SOR_ART")){
        FILE *f=fopen(path,"rb");if(!f)throw std::runtime_error("SOR_ART open failed");
        int c;while((c=fgetc(f))!=EOF)capture.package.push_back(uint8_t(c));fclose(f);
        if(!capture.art.load(capture.package.data(),capture.package.size()))throw std::runtime_error("SOR_ART package invalid");
        printf("ART %zu frames in %zu pages\n",capture.art.frames().size(),capture.art.pages().size());
    }
    if(const char *spec=std::getenv("SOR_ENHANCED_CAPTURE")){
        std::string s(spec);const auto a=s.rfind(':'),b=s.rfind(':',a-1),c=s.rfind(':',b-1);
        capture.directory=s.substr(0,c);capture.first=std::stoul(s.substr(c+1,b-c-1));
        capture.last=std::stoul(s.substr(b+1,a-b-1));capture.step=std::max(1ul,std::stoul(s.substr(a+1)));
        capture.scene=std::make_unique<sor::VdpScene>();
    }
}
void capture_enhanced(VDPState &state){
    capture_setup();
    capture.frame++;capture.pending=false;
    if(!capture.scene||capture.frame<capture.first||capture.frame>capture.last||(capture.frame-capture.first)%capture.step)return;
    const auto status=state.status_;   // scene building sets sprite status bits; keep the game's
    capture.scene->enhanced=true;capture.scene->art=&capture.art;
    static const bool smooth=std::getenv("SOR_SMOOTH")&&std::getenv("SOR_SMOOTH")[0]=='1';
    capture.scene->smooth=smooth;
    static const bool lighting=!(std::getenv("SOR_LIGHTING")&&std::getenv("SOR_LIGHTING")[0]=='0');
    static const char weather=std::getenv("SOR_WEATHER")?std::getenv("SOR_WEATHER")[0]:'0';
    capture.scene->lighting=lighting&&gamePlaying;capture.scene->weather=(weather=='1'||weather=='2')&&gamePlaying;capture.scene->round=gameRound;
    if(weather=='2'&&capture.frame==capture.first+2)capture.scene->weatherStrike();
    const bool ok=capture.scene->build(state);
    state.status_=status;
    if(!ok)return;
    capture.width=capture.scene->width*2;capture.height=capture.scene->height*2;
    capture.image.assign(size_t(capture.width)*capture.height,0);
    sor::raster_enhanced(*capture.scene,state,capture.image.data(),capture.width);
    capture.pending=true;
}
void capture_write(const Framebuffer &fb,int width,int height){
    if(!capture.pending)return;
    capture.pending=false;
    char name[32];snprintf(name,sizeof name,"/frame-%06u.ppm",capture.frame);
    FILE *out=fopen((capture.directory+name).c_str(),"wb");if(!out)throw std::runtime_error("Enhanced capture open failed");
    const int w=width*2+capture.width,h=std::max(height*2,capture.height);
    fprintf(out,"P6\n%d %d\n255\n",w,h);
    const auto *b=static_cast<const uint8_t*>(fb.getRawPointer());
    std::vector<uint8_t> row(size_t(w)*3);
    for(int y=0;y<h;y++){
        std::fill(row.begin(),row.end(),0);
        for(int x=0;x<width*2&&y<height*2;x++){const auto *p=b+(y/2)*Framebuffer::PITCH+(x/2)*3;
            for(int k=0;k<3;k++)row[x*3+k]=uint8_t(p[2-k]*255/7);}
        for(int x=0;x<capture.width&&y<capture.height;x++){const uint16_t c=capture.image[y*capture.width+x];
            const int o=(width*2+x)*3;row[o]=uint8_t((c>>10&31)*255/31);row[o+1]=uint8_t((c>>5&31)*255/31);row[o+2]=uint8_t((c&31)*255/31);}
        fwrite(row.data(),1,row.size(),out);
    }
    fclose(out);
    // The art drawn in this frame, for checking what replaced what.
    snprintf(name,sizeof name,"/frame-%06u.txt",capture.frame);
    if(FILE *list=fopen((capture.directory+name).c_str(),"w")){
        const auto &scene=*capture.scene;
        for(size_t i=0;i<scene.artCount;i++){const auto &d=scene.artDraws[i];const auto &f=capture.art.frames()[d.frame];
            fprintf(list,"art %06X c%04X type %02X anchor %d,%d layer %d order %d%s size %dx%d",f.mapping,f.colours,d.type,d.x,d.y,d.layer,d.order,d.flip?" flip":"",f.w,f.h);
            if(d.lit){
                fprintf(list," ground %d",d.ground);
                for(const auto &shadow:d.light.shadow)fprintf(list," shadow %d/%d@%d",shadow.lean,shadow.length,shadow.alpha);
                fputs(" light",list);
                for(int i=0;i<4;i++){const auto &c=d.light.corner(i);fprintf(list," %d,%d,%d+%d,%d,%d",c.scale[0],c.scale[1],c.scale[2],c.offset[0],c.offset[1],c.offset[2]);}
                for(const auto &rim:d.light.rim)fprintf(list," rim@%d",rim.alpha);
            }
            if(f.from)fprintf(list," in-between from %06X",f.from);
            fputc('\n',list);}
        for(size_t i=0;i<scene.glowCount;i++){const auto &g=scene.glows[i];
            fprintf(list,"glow %d,%d radius %dx%d colour %d,%d,%d strength %d\n",g.x,g.y,g.radiusX,g.radiusY,g.colour[0],g.colour[1],g.colour[2],g.strength);}
        if(scene.lighting){
            // The backdrop's light grid, a row of cells per line (rrggbb).
            const auto &light=scene.sceneLight();
            fprintf(list,"ambient %d,%d,%d level %d wall %d lights %u\n",light.ambient[0],light.ambient[1],light.ambient[2],light.level(),light.wallLine(),light.lightCount());
            for(unsigned i=0;i<light.lightCount();i++){const auto &l=light.lights()[i];
                fprintf(list,"light %d,%d power %u colour %d,%d,%d%s\n",l.x,l.y,l.power,l.colour[0],l.colour[1],l.colour[2],l.low?" lamp":"");}
            fputs("spill",list);
            for(int i=0;i<=sor::SceneLight::COLS;i++)fprintf(list," %d",light.spill(i).strength);
            fputc('\n',list);
            for(int row=0;row<sor::SceneLight::ROWS;row++){
                fputs("grid",list);
                for(int column=0;column<sor::SceneLight::COLS;column++){const uint8_t *c=light.cell(row,column);fprintf(list," %02x%02x%02x",c[0],c[1],c[2]);}
                fputc('\n',list);
            }
        }
        {unsigned covering=0;for(size_t i=0;i<scene.particleCount;i++)covering+=!scene.particleDraws[i].additive;
         fprintf(list,"particles %zu (covering %u)\n",scene.particleCount,covering);}
        if(scene.weatherOn()){
            const auto &p=scene.weatherProfile();
            fprintf(list,"weather rain %d wet %d fog %d mist %d shafts %d lightning %d flash %d time %u quads %zu\n",
                    p.rain,p.wet,p.fog,p.mist,p.shafts,p.lightning,scene.weatherState().flash(),scene.weatherState().time(),scene.weatherQuadCount);
            for(size_t i=0;i<scene.weatherQuadCount;i++){const auto &q=scene.weatherQuads[i];
                fprintf(list,"weather quad %s %s %s x %d,%d,%d,%d y %d-%d alpha %d,%d,%d,%d colour %d,%d,%d\n",
                        q.texture==sor::WeatherTexture::STREAK?"streak":q.texture==sor::WeatherTexture::NOISE?"noise":"flat",
                        q.depth==sor::WeatherQuad::BEHIND?"behind":q.depth==sor::WeatherQuad::GROUND?"ground":"front",q.additive?"added":"covering",
                        q.x[0],q.x[1],q.x[2],q.x[3],q.y[0],q.y[2],q.alpha[0],q.alpha[1],q.alpha[2],q.alpha[3],q.colour[0],q.colour[1],q.colour[2]);}
        }
        fprintf(list,"sprite cells %zu\n",scene.spriteTileCount);fclose(list);
    }
}
// The framebuffer (3-bit RGB) as a binary PPM.
void write_ppm(const Framebuffer &fb,const std::string &path,const char *what){
    FILE *out=fopen(path.c_str(),"wb");
    if(!out)throw std::runtime_error(std::string(what)+" open failed");
    fprintf(out,"P6\n320 224\n255\n");
    const auto *pixels=static_cast<const uint8_t*>(fb.getRawPointer());
    for(int i=0;i<320*224;i++){
        const uint8_t rgb[]={uint8_t(pixels[i*3+2]*255/7),uint8_t(pixels[i*3+1]*255/7),uint8_t(pixels[i*3]*255/7)};
        if(fwrite(rgb,1,3,out)!=3){fclose(out);throw std::runtime_error(std::string(what)+" write failed");}
    }
    if(fclose(out))throw std::runtime_error(std::string(what)+" close failed");
}
}
const uint8_t *platform_embedded_rom(size_t &size){size=0;return nullptr;}
const uint8_t *platform_embedded_art(size_t &size){size=0;return nullptr;}
void platform_game_state(unsigned round,unsigned,bool playing){gameRound=round;gamePlaying=playing;sor::extract_frames_round(playing?round:0);}
void platform_video_init(){}
void platform_video_shutdown(){}
bool platform_render_vdp(VDPState &state,const sor::TitleCaption &title){
    static unsigned presented=0;
    sor::extract_frames(state,++presented);
    capture_enhanced(state);
    if(std::getenv("SOR_VALIDATE_GPU_SCENE")){
        if(!scene)scene=std::make_unique<sor::VdpScene>();
        sceneState=scene->buildCached(state)?&state:nullptr;
        sceneTitle=title;
    }
    return false;
}
void platform_video_present(const Framebuffer &fb,int width,int height){
    capture_write(fb,width,height);
    if(!sceneState)return;
    uint16_t expected[320*240];sor::raster_scene(*scene,*sceneState,expected);
    sceneTitle.draw([&](int x,int y,unsigned r,unsigned g,unsigned b){expected[y*320+x]=sor::VdpScene::rgb1555(r,g,b);});
    const auto *b=static_cast<const uint8_t*>(fb.getRawPointer());
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        const auto *p=b+y*Framebuffer::PITCH+x*3;
        if(expected[y*320+x]!=sor::VdpScene::rgb1555(p[2],p[1],p[0])){
            fprintf(stderr,"GPU scene differs at frame %u pixel %d,%d\n",sceneFrames,x,y);
            throw std::runtime_error("GPU scene pixel mismatch");
        }
    }
    sceneFrames++;
}
void platform_poll_controllers(PlayersControlState &){throw std::runtime_error("Headless replay exhausted or missing");}
void platform_cheat_menu_present(const Framebuffer &fb){
    if(!replay_finished())return;
    write_ppm(fb,capturePath,"Menu capture");
    throw ReplayFinished{};
}
uint64_t platform_time_us(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
PlatformMemoryStats platform_memory_stats(){return {};}
void platform_frame_parts(uint32_t,uint32_t){}
void platform_observe_frame(uint32_t frame,const sor_memory &memory,const Framebuffer &fb){
    if(memory.faults) throw std::runtime_error("Unmapped device access in headless simulation");
    if(fwrite(memory.ram,1,sizeof(memory.ram),trace)!=sizeof(memory.ram)) throw std::runtime_error("Trace write failed");
    if(replay_finished()){
        write_ppm(fb,capturePath,"Capture");
        throw ReplayFinished{};
    }
}
int main(int argc,char **argv){
    if(argc!=4){fprintf(stderr,"usage: sor-headless ROM REPLAY RAMTRACE\n");return 2;}
    // SOR_CHEATS=ROUND: start at that round with infinite health, lives and specials.
    if(const char *round=std::getenv("SOR_CHEATS"))sor::cheats::menu.setScripted(unsigned(std::atoi(round)));
    sor::extract_frames_rom(argv[1]);
    if(!replay_load(argv[2])){fprintf(stderr,"Invalid replay\n");return 2;}
    trace=fopen(argv[3],"wb");if(!trace){perror("trace");return 2;}
    capturePath=std::string(argv[3])+".ppm";
    int result=0;
    try {auto game=std::make_unique<StreetsOfRage>(argv[1]);game->boot();result=1;}
    catch(const ReplayFinished &){printf("Replay complete; GPU scenes checked %u\n",sceneFrames);}
    catch(const std::exception &error){fprintf(stderr,"Simulation failed: %s\n",error.what());result=1;}
    if(fclose(trace))result=1;
    return result;
}

namespace {FILE *audioCapture=nullptr;}
void platform_audio_init(unsigned){if(const char *path=std::getenv("SOR_AUDIO_CAPTURE")){audioCapture=fopen(path,"wb");if(!audioCapture)throw std::runtime_error("Audio capture open failed");}}
void platform_audio_submit(const int16_t *samples,unsigned frames,const int16_t *dac){
    int16_t mixed[NativeAudio::maxFrameSamples*2];if(dac){for(unsigned i=0;i<frames*2;i++)mixed[i]=int(samples[i])+dac[i];samples=mixed;}
    if(audioCapture && fwrite(samples,4,frames,audioCapture)!=frames)throw std::runtime_error("Audio capture failed");}
void platform_audio_shutdown(){if(audioCapture){fclose(audioCapture);audioCapture=nullptr;}}

bool platform_audio_enabled(){const char *p=std::getenv("SOR_AUDIO");return !p || std::string(p)!="0";}

bool platform_audio_native_dac(){const char *p=std::getenv("SOR_DAC_NATIVE");return !p || std::string(p)!="0";}

bool platform_audio_split_dac(){const char *p=std::getenv("SOR_DAC_AICA");return p && std::string(p)=="1";}

bool platform_audio_profile(){const char *p=std::getenv("SOR_AUDIO_PROFILE");return p && p[0]=='1';}
