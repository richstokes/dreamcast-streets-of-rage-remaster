#include "SoR.hpp"
#include "platform.hpp"
#include "replay.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct ReplayFinished {};
FILE *trace=nullptr;
std::string capturePath;
}
void platform_video_init(){}
void platform_video_shutdown(){}
void platform_video_present(const Framebuffer &,int,int){}
void platform_poll_controllers(PlayersControlState &){throw std::runtime_error("Headless replay exhausted or missing");}
uint64_t platform_time_us(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
PlatformMemoryStats platform_memory_stats(){return {};}
void platform_observe_frame(uint32_t frame,const sor_memory &memory,const Framebuffer &fb){
    if(memory.faults) throw std::runtime_error("Unmapped device access in headless simulation");
    if(fwrite(memory.ram,1,sizeof(memory.ram),trace)!=sizeof(memory.ram)) throw std::runtime_error("Trace write failed");
    if(frame>=replay_total_frames()){
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
    catch(const ReplayFinished &){printf("Completed %u frames\n",replay_total_frames());}
    catch(const std::exception &error){fprintf(stderr,"Simulation failed: %s\n",error.what());result=1;}
    if(fclose(trace))result=1;
    return result;
}
