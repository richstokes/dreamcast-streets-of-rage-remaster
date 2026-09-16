#include "diagnostics.hpp"
#include "dac_aica.hpp"
#include <kos.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/aica_comm.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
namespace {
constexpr unsigned mainFrames=16384,chipFrames=8192,chunkFrames=4096;
alignas(32) int16_t ring[mainFrames][4],planar[4][chunkFrames];
int channels[4]{-1,-1,-1,-1};uint32_t memory[4]{};
unsigned readAt=0,writeAt=0,queued=0,rate=0,lastPosition=0,calls=0,underruns=0,overruns=0,resyncs=0;
uint64_t written=0,played=0,lastPoll=0,uploaded=0,transferTime=0,transferWorst=0;unsigned transfers=0;bool playing=false;
void sendStop(){
    AICA_CMDSTR_CHANNEL(packet,command,channel);
    std::memset(packet,0,sizeof(packet));
    command->cmd=AICA_CMD_CHAN;command->size=AICA_CMDSTR_CHANNEL_SIZE;channel->cmd=AICA_CH_CMD_STOP;
    for(int c:channels)if(c>=0){command->cmd_id=c;snd_sh4_to_aica(packet,command->size);}
    snd_sh4_to_aica_start();playing=false;
}
void transfer(unsigned frames){
    uint64_t begin=timer_us_gettime64();
    while(frames){
        unsigned offset=written%chipFrames;
        unsigned n=std::min({frames,chunkFrames,chipFrames-offset});
        if(queued<n)underruns++;
        for(unsigned i=0;i<n;i++){
            if(queued){for(unsigned c=0;c<4;c++)planar[c][i]=ring[readAt][c];readAt=(readAt+1)%mainFrames;queued--;}
            else for(unsigned c=0;c<4;c++)planar[c][i]=0;
        }
        for(unsigned c=0;c<4;c++){
            dcache_purge_range((uintptr_t)planar[c],n*2);
            int result;
            do {result=spu_dma_transfer(planar[c],memory[c]+offset*2,n*2,1,nullptr,nullptr);if(result<0&&errno==EINPROGRESS)thd_pass();}
            while(result<0&&errno==EINPROGRESS);
            if(result<0)throw std::runtime_error("DAC AICA DMA failed");
        }
        uploaded+=n*8;written+=n;frames-=n;
    }
    uint64_t duration=timer_us_gettime64()-begin;transferTime+=duration;transferWorst=std::max(transferWorst,duration);transfers++;
}
void start(){
    written=played=lastPosition=0;transfer(chipFrames);
    AICA_CMDSTR_CHANNEL(packet,command,channel);
    std::memset(packet,0,sizeof(packet));
    snd_sh4_to_aica_stop();uint32_t mask=0;
    for(unsigned c=0;c<4;c++){
        command->cmd=AICA_CMD_CHAN;command->size=AICA_CMDSTR_CHANNEL_SIZE;command->cmd_id=channels[c];
        channel->cmd=AICA_CH_CMD_START|AICA_CH_START_DELAY;
        channel->base=memory[c];channel->type=AICA_SM_16BIT;channel->length=chipFrames;
        channel->loop=1;channel->loopstart=0;channel->loopend=chipFrames;channel->freq=rate;
        channel->vol=255;channel->pan=(c&1)?255:0;
        snd_sh4_to_aica(packet,command->size);mask|=uint32_t(1)<<channels[c];
    }
    // One hardware key-on mask starts all four voices on the same sample edge.
    command->cmd_id=mask;channel->cmd=AICA_CH_CMD_START|AICA_CH_START_SYNC;
    snd_sh4_to_aica(packet,command->size);snd_sh4_to_aica_start();playing=true;lastPoll=timer_us_gettime64();
}
}
void dac_aica_init(unsigned sampleRate){
    if(snd_init()<0)throw std::runtime_error("DAC AICA init failed");
    rate=sampleRate;readAt=writeAt=queued=calls=underruns=overruns=resyncs=transfers=0;
    written=played=uploaded=transferTime=transferWorst=0;playing=false;
    for(unsigned c=0;c<4;c++){
        channels[c]=snd_sfx_chn_alloc();memory[c]=snd_mem_malloc(chipFrames*2);
        // Stock KOS START_SYNC carries a 32-bit channel mask. Reject higher
        // allocations instead of silently dropping voices from synchronized start.
        if(channels[c]<0||channels[c]>=32||!memory[c]){dac_aica_shutdown();throw std::runtime_error("DAC AICA channels/memory unavailable");}
    }
    sor_log("AICA_DAC: four synchronized voices, 65536 sound-RAM bytes, available=%lu\n",(unsigned long)snd_mem_available());
}
void dac_aica_submit(const int16_t *fm,const int16_t *dac,unsigned frames){
    if(!dac)throw std::runtime_error("Missing DAC stem");
    if(frames>mainFrames-queued){overruns++;frames=mainFrames-queued;}
    for(unsigned i=0;i<frames;i++){
        ring[writeAt][0]=fm[i*2];ring[writeAt][1]=fm[i*2+1];ring[writeAt][2]=dac[i*2];ring[writeAt][3]=dac[i*2+1];
        writeAt=(writeAt+1)%mainFrames;
    }
    queued+=frames;
    if(!playing){if(queued>=chipFrames)start();}
    else {
        uint64_t now=timer_us_gettime64();
        if(now-lastPoll>=uint64_t(chipFrames)*1000000/rate){
            // A complete ring may have elapsed; a modulo position cannot tell
            // how many wraps occurred. Restart instead of treating stale data as new.
            sendStop();resyncs++;queued=readAt=writeAt=0;
        }else{
            unsigned position=snd_get_pos(channels[0])%chipFrames;
            played+=(position-lastPosition)&(chipFrames-1);lastPosition=position;
            unsigned free=unsigned(played+chipFrames-written)&~15u; // DMA is 32-byte aligned per mono channel.
            if(free>chipFrames)throw std::runtime_error("DAC AICA ring cursor invalid");
            if(free)transfer(free);
        }
        lastPoll=now;
    }
    if(++calls%600==0)dac_aica_report();
}
void dac_aica_shutdown(){
    if(playing)sendStop();
    for(unsigned c=0;c<4;c++){
        if(channels[c]>=0){snd_sfx_chn_free(channels[c]);channels[c]=-1;}
        if(memory[c]){snd_mem_free(memory[c]);memory[c]=0;}
    }
}

void dac_aica_report(){
    sor_log("AICA_DAC queued=%u underruns=%u overruns=%u resyncs=%u uploaded=%llu positions=%u/%u/%u/%u transfer_mean_us=%llu transfer_max_us=%llu\n",queued,underruns,overruns,resyncs,(unsigned long long)uploaded,snd_get_pos(channels[0]),snd_get_pos(channels[1]),snd_get_pos(channels[2]),snd_get_pos(channels[3]),(unsigned long long)(transfers?transferTime/transfers:0),(unsigned long long)transferWorst);
}
