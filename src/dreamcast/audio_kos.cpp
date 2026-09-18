#include "diagnostics.hpp"
#include <kos.h>
#include "sor_audio_config.hpp"
#include "dac_aica.hpp"
#include "platform.hpp"
#include <dc/sound/stream.h>
#include <dc/sound/sound.h>
#include <algorithm>
#include <stdexcept>
namespace {
// Stream design (docs/AUDIO.md):
// - The game thread renders 888-889 frames per video frame into `ring`.
// - A feeder thread polls KOS every 2 ms, so sound RAM needs only a small
//   2048-frame double buffer (1024-frame refills) even during long frames.
//   KOS skips refills under half the buffer and rounds to 512 frames, so a
//   1024-frame buffer under-fills (measured: stale replay, ring overflow).
// - The ring holds a cushion for production gaps (frame bursts, transitions).
//   On a shortfall, play silence until the cushion is restored: one gap per
//   production stall instead of a run of short ones.
// - Production follows the display (888.93 frames per VBlank) and playback the
//   AICA clock. Flycast measures 59.81 Hz VBlanks, hardware NTSC ~59.94 Hz,
//   so neither matches exactly. Each refill consumes up to 4 frames more or
//   fewer than it outputs (nearest neighbour, <0.4%, <7 cents), proportional
//   to the ring's distance from its target, to hold the ring near the cushion.
constexpr unsigned capacity=16384,streamFrames=2048,cushion=3584;
constexpr int maxAdjust=4,adjustScale=256;
// AICA plays 44100*(1+fns/1024) Hz; 53274 selects fns=213 (53273.1 Hz), the
// step nearest to production. 53267 would select 53230.1 Hz.
constexpr unsigned playbackRate=53274;
alignas(32) int16_t ring[capacity*2],output[streamFrames*2];
// Single producer (game thread) / single consumer (feeder thread). Aligned
// 32-bit loads and stores are atomic on SH-4; each index has one writer.
volatile uint32_t written=0,consumed=0;
uint64_t requestedBytes=0,silentFrames=0,producedFrames=0,startUs=0,levelSum=0;
unsigned callbacks=0,startVblanks=0,underruns=0,overruns=0,calls=0,dropped=0,repeated=0,levelMin=~0u,levelMax=0,levelSamples=0;
snd_stream_hnd_t stream=SND_STREAM_INVALID;
volatile bool playing=false,stopping=false;
bool refilling=false;
kthread_t *feeder=nullptr;
// Frame and ring level at the first underruns, for the benchmark report.
volatile unsigned currentSubmit=0;unsigned underrunAt[16],underrunLevel[16];
unsigned queued(){return written-consumed;}
// Single core: ordering ring accesses against the index hand-off only needs
// the compiler not to move memory accesses across these points.
inline void barrier(){asm volatile("":::"memory");}
void *callback(snd_stream_hnd_t,int requested,int *received){
    // KOS 2.3 snd_stream_fill passes byte counts despite the header
    // callback parameter names saying samples. Stereo PCM16 is four bytes/frame.
    if(requested<=0){*received=0;return nullptr;}
    requestedBytes+=requested;callbacks++;
    const unsigned n=std::min(unsigned(requested)/4,streamFrames);
    unsigned available=queued(),silent=0,read=consumed;
    barrier();
    if(available<n && !refilling){
        if(underruns<16){underrunAt[underruns]=currentSubmit;underrunLevel[underruns]=available;}
        underruns++;refilling=true;
    }
    if(refilling && available<cushion+n){
        // Hold the ring; the frames it has are played once it is full again.
        std::fill_n(output,n*2,int16_t(0));silentFrames+=n;
    }else{
        refilling=false;
        const int error=int(available)-int(cushion);
        const int adjust=std::clamp(error/adjustScale,-maxAdjust,std::min(maxAdjust,int(available-n)));
        const unsigned take=n+adjust;
        if(adjust>0)dropped+=adjust;else repeated+=-adjust;
        // Nearest-neighbour step of take/n ring frames per output frame.
        for(unsigned i=0,position=0;i<n;i++,position+=take){
            const unsigned at=(read+position/n)%capacity;
            output[i*2]=ring[at*2];output[i*2+1]=ring[at*2+1];
        }
        read+=take;
    }
    barrier();
    consumed=read;
    *received=n*4;return output;
}
void *feed(void*){
    while(!stopping){snd_stream_poll(stream);thd_sleep(2);}
    return nullptr;
}
}
bool platform_audio_split_dac(){return SOR_ENABLE_AICA_DAC!=0;}
bool platform_audio_native_dac(){return SOR_ENABLE_NATIVE_DAC!=0;}
bool platform_audio_profile(){return SOR_ENABLE_AUDIO_PROFILE!=0;}
bool platform_audio_enabled(){return SOR_ENABLE_EXPERIMENTAL_AUDIO!=0;}
void platform_audio_init(unsigned rate){
    if(!platform_audio_enabled())return;
    if(platform_audio_split_dac()){dac_aica_init(rate);return;}
    if(rate!=53267)throw std::runtime_error("AICA playback rate assumes 53267 Hz synthesis");
    if(snd_stream_init_ex(2,streamFrames*2)<0)throw std::runtime_error("AICA stream init failed");
    stream=snd_stream_alloc(callback,streamFrames*2);
    if(stream==SND_STREAM_INVALID)throw std::runtime_error("AICA stream allocation failed");
    sor_log("AICA stream: %u sound-RAM bytes; available=%lu\n",streamFrames*4,(unsigned long)snd_mem_available());
}
void platform_audio_submit(const int16_t *samples,unsigned frames,const int16_t *dac){
    if(platform_audio_split_dac()){dac_aica_submit(samples,dac,frames);return;}
    const unsigned level=queued();
    if(playing){levelSum+=level;levelSamples++;levelMin=std::min(levelMin,level);levelMax=std::max(levelMax,level);}
    if(frames>capacity-level){overruns++;frames=capacity-level;}
    unsigned at=written;
    for(unsigned i=0;i<frames;i++,at++){ring[(at%capacity)*2]=samples[i*2];ring[(at%capacity)*2+1]=samples[i*2+1];}
    barrier();
    written=at;
    if(playing)producedFrames+=frames;
    if(!playing && queued()>=streamFrames+cushion){
        snd_stream_start(stream,playbackRate,1);
        pvr_stats_t stats;pvr_get_stats(&stats);startVblanks=stats.vbl_count;startUs=timer_us_gettime64();requestedBytes=0;
        underruns=0;silentFrames=0;
        playing=true;
        feeder=thd_create(false,feed,nullptr);
        if(!feeder)throw std::runtime_error("AICA feeder thread failed");
        thd_set_prio(feeder,PRIO_DEFAULT-2);
    }
    currentSubmit=++calls;
    if(calls%600==0)platform_audio_report();
}
void platform_audio_shutdown(){
    if(platform_audio_split_dac()){dac_aica_shutdown();return;}
    if(feeder){stopping=true;thd_join(feeder,nullptr);feeder=nullptr;}
    if(stream!=SND_STREAM_INVALID){snd_stream_destroy(stream);snd_stream_shutdown();stream=SND_STREAM_INVALID;}
}

void platform_audio_report(){
    if(!platform_audio_enabled())return;
    if(platform_audio_split_dac()){dac_aica_report();return;}
    pvr_stats_t stats;pvr_get_stats(&stats);
    // Ring level sampled before each submit; add the sound-RAM buffer
    // (1024-2048 frames) for the delay from synthesis to the DAC.
    sor_log("AICA queued=%u level_min=%u level_mean=%llu level_max=%u underruns=%u silent_frames=%llu dropped=%u repeated=%u overruns=%u callbacks=%u consumed=%llu produced=%llu since_start_us=%llu vblanks=%lu submits=%u\n",
        queued(),levelSamples?levelMin:0,(unsigned long long)(levelSamples?levelSum/levelSamples:0),levelMax,underruns,(unsigned long long)silentFrames,dropped,repeated,overruns,callbacks,
        (unsigned long long)(requestedBytes/4),(unsigned long long)producedFrames,(unsigned long long)(playing?timer_us_gettime64()-startUs:0),(unsigned long)(stats.vbl_count-startVblanks),calls);
    for(unsigned i=0;i<std::min(underruns,16u);i++)sor_log("AICA_UNDERRUN submit=%u level=%u\n",underrunAt[i],underrunLevel[i]);
}
