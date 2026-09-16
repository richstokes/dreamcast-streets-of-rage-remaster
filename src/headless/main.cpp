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

namespace {
struct ReplayFinished {};
FILE *trace=nullptr;
std::string capturePath;
std::unique_ptr<sor::VdpScene> scene;
VDPState *sceneState=nullptr;
unsigned sceneFrames=0;
}
const uint8_t *platform_embedded_rom(size_t &size){size=0;return nullptr;}
void platform_video_init(){}
void platform_video_shutdown(){}
bool platform_render_vdp(VDPState &state,VDPRenderer &renderer){
    if(std::getenv("SOR_VALIDATE_GPU_SCENE")){
        if(!scene)scene=std::make_unique<sor::VdpScene>();
        sceneState=scene->buildCached(state,renderer)?&state:nullptr;
    }
    return false;
}
void platform_video_present(const Framebuffer &fb,int width,int height){
    if(!sceneState)return;
    uint16_t expected[320*240];sor::raster_scene(*scene,*sceneState,expected);
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
uint64_t platform_time_us(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
PlatformMemoryStats platform_memory_stats(){return {};}
void platform_observe_frame(uint32_t frame,const sor_memory &memory,const Framebuffer &fb){
    if(memory.faults) throw std::runtime_error("Unmapped device access in headless simulation");
    if(fwrite(memory.ram,1,sizeof(memory.ram),trace)!=sizeof(memory.ram)) throw std::runtime_error("Trace write failed");
    if(replay_finished()){
        FILE *capture=fopen(capturePath.c_str(),"wb");
        if(!capture) throw std::runtime_error("Capture open failed");
        fprintf(capture,"P6\n320 224\n255\n");
        const auto *pixels=static_cast<const uint8_t*>(fb.getRawPointer());
        for(int i=0;i<320*224;i++){
            const uint8_t rgb[]={uint8_t(pixels[i*3+2]*255/7),uint8_t(pixels[i*3+1]*255/7),uint8_t(pixels[i*3]*255/7)};
            if(fwrite(rgb,1,3,capture)!=3){fclose(capture);throw std::runtime_error("Capture write failed");}
        }
        if(fclose(capture)) throw std::runtime_error("Capture close failed");
        throw ReplayFinished{};
    }
}
int main(int argc,char **argv){
    if(argc!=4){fprintf(stderr,"usage: sor-headless ROM REPLAY RAMTRACE\n");return 2;}
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
    int16_t mixed[1780];if(dac){for(unsigned i=0;i<frames*2;i++)mixed[i]=int(samples[i])+dac[i];samples=mixed;}
    if(audioCapture && fwrite(samples,4,frames,audioCapture)!=frames)throw std::runtime_error("Audio capture failed");}
void platform_audio_shutdown(){if(audioCapture){fclose(audioCapture);audioCapture=nullptr;}}

bool platform_audio_enabled(){const char *p=std::getenv("SOR_AUDIO");return !p || std::string(p)!="0";}

bool platform_audio_native_dac(){const char *p=std::getenv("SOR_DAC_NATIVE");return !p || std::string(p)!="0";}

bool platform_audio_split_dac(){const char *p=std::getenv("SOR_DAC_AICA");return p && std::string(p)=="1";}
