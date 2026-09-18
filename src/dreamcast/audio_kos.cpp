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
constexpr unsigned capacity=16384;
// KOS double-buffers 8192 frames in sound RAM and fills both halves on start,
// then requests 4096-frame halves. Keep a cushion beyond that so ordinary
// frame-to-frame jitter never meets a request with a partly filled ring.
constexpr unsigned streamFrames=8192,cushion=2048;
alignas(32) int16_t ring[capacity*2],output[4096*2];
uint64_t requestedBytes=0,silentFrames=0,producedFrames=0,startUs=0;unsigned callbacks=0,startVblanks=0;
unsigned readAt=0,writeAt=0,queued=0,underruns=0,overruns=0,calls=0;
snd_stream_hnd_t stream=SND_STREAM_INVALID;
bool starting=true,playing=false;unsigned sampleRate=0;
void *callback(snd_stream_hnd_t,int requested,int *received){
    // KOS 2.3 snd_stream_fill passes byte counts despite the header
    // callback parameter names saying samples. Stereo PCM16 is four bytes/frame.
    if(requested<=0){*received=0;return nullptr;}
    requestedBytes+=requested;callbacks++;
    unsigned n=std::min(unsigned(requested)/4,4096u),silent=0;
    // On a shortfall, lead with enough silence to restore the cushion. One
    // gap then absorbs the deficit instead of every later request running short.
    if(queued<n){if(!starting)underruns++;silent=std::min(n,n-queued+cushion);silentFrames+=silent;}
    for(unsigned i=0;i<n;i++){
        if(i>=silent){output[i*2]=ring[readAt*2];output[i*2+1]=ring[readAt*2+1];readAt=(readAt+1)%capacity;queued--;}
        else output[i*2]=output[i*2+1]=0;
    }
    *received=n*4;return output;
}
}
bool platform_audio_split_dac(){return SOR_ENABLE_AICA_DAC!=0;}
bool platform_audio_native_dac(){return SOR_ENABLE_NATIVE_DAC!=0;}
bool platform_audio_profile(){return SOR_ENABLE_AUDIO_PROFILE!=0;}
bool platform_audio_enabled(){return SOR_ENABLE_EXPERIMENTAL_AUDIO!=0;}
void platform_audio_init(unsigned rate){
    if(!platform_audio_enabled())return;
    if(platform_audio_split_dac()){dac_aica_init(rate);return;}
    if(snd_stream_init_ex(2,streamFrames*2)<0)throw std::runtime_error("AICA stream init failed");
    stream=snd_stream_alloc(callback,streamFrames*2);
    if(stream==SND_STREAM_INVALID)throw std::runtime_error("AICA stream allocation failed");
    sampleRate=rate;
    sor_log("AICA stream: 32768 sound-RAM bytes; available=%lu\n",(unsigned long)snd_mem_available());
}
void platform_audio_submit(const int16_t *samples,unsigned frames,const int16_t *dac){
    if(platform_audio_split_dac()){dac_aica_submit(samples,dac,frames);return;}
    if(frames>capacity-queued){overruns++;frames=capacity-queued;}
    for(unsigned i=0;i<frames;i++){
        ring[writeAt*2]=samples[i*2];ring[writeAt*2+1]=samples[i*2+1];writeAt=(writeAt+1)%capacity;
    }
    queued+=frames;
    if(playing)producedFrames+=frames;
    if(!playing && queued>=streamFrames+cushion){
        snd_stream_start(stream,sampleRate,1);starting=false;playing=true;
        pvr_stats_t stats;pvr_get_stats(&stats);startVblanks=stats.vbl_count;startUs=timer_us_gettime64();requestedBytes=0;
    }
    if(playing)snd_stream_poll(stream);
    if(++calls%600==0)platform_audio_report();
}
void platform_audio_shutdown(){if(platform_audio_split_dac()){dac_aica_shutdown();return;}if(stream!=SND_STREAM_INVALID){snd_stream_destroy(stream);snd_stream_shutdown();stream=SND_STREAM_INVALID;}}

void platform_audio_report(){
    if(!platform_audio_enabled())return;
    if(platform_audio_split_dac()){dac_aica_report();return;}
    pvr_stats_t stats;pvr_get_stats(&stats);
    sor_log("AICA queued=%u underruns=%u silent_frames=%llu overruns=%u callbacks=%u consumed=%llu produced=%llu since_start_us=%llu vblanks=%lu submits=%u\n",queued,underruns,(unsigned long long)silentFrames,overruns,callbacks,(unsigned long long)(requestedBytes/4),(unsigned long long)producedFrames,(unsigned long long)(playing?timer_us_gettime64()-startUs:0),(unsigned long)(stats.vbl_count-startVblanks),calls);
}
