#include "dac_driver.hpp"
#include <algorithm>
bool NativeDacDriver::recognizes(const uint8_t *ram){
    // Code identity only: exclude the mutable delta table and previous delta.
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<0x19b;i++)if(i<0x1e||i>=0x2f)hash=(hash^ram[i])*16777619u;
    return hash==0xdbb952e7u;
}
void NativeDacDriver::start(uint16_t data,uint16_t bytes,uint16_t descriptor,uint8_t accumulator,uint16_t stack){
    pointer_=data;remaining_=bytes;descriptor_=descriptor;accumulator_=accumulator;stack_=stack;
    active_=true;phase(High);
}
const NativeDacDriver::Step NativeDacDriver::highProgram_[]={{7,ReadHigh},{7,None},{4,None},{10,Choose}};
const NativeDacDriver::Step NativeDacDriver::lowProgram_[]={{7,None},{4,None},{10,Choose}};
const NativeDacDriver::Step NativeDacDriver::highNormalProgram_[]={
        {4,None},{4,None},{4,None},{4,None},{7,None},{4,None},{4,None},
        {7,Delta},{13,StoreDelta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{7,Data},{6,None},{0,Delay},{4,None},{13,BusyOff},
        {4,None},{7,ReadLow}};
const NativeDacDriver::Step NativeDacDriver::lowNormalProgram_[]={
        {4,None},{4,None},{4,None},{4,None},{7,None},{4,None},{4,None},
        {7,Delta},{13,StoreDelta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{4,None},{7,Data},{6,None},{0,Delay},{13,BusyOff}};
const NativeDacDriver::Step NativeDacDriver::zeroSetupProgram_[]={{13,RepeatCount},{4,None}};
const NativeDacDriver::Step NativeDacDriver::zeroLoopProgram_[]={
        {4,None},{4,None},{4,None},{4,None},{4,None},{4,None},{4,None},{17,Call},
        {13,Delta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{4,None},{7,Data},{6,None},{0,Delay},
        {13,BusyOff},{10,None},{4,None},{4,RepeatDec},{10,RepeatDone},
        {4,None},{4,None},{4,None},{4,None},{10,RepeatAgain}};
const NativeDacDriver::Step NativeDacDriver::highTailProgram_[]={{4,None},{7,ReadLow}};
const NativeDacDriver::Step NativeDacDriver::finishByteProgram_[]={{4,None},{12,Command},{10,Interrupt},{6,NextByte},{6,CountByte},{4,None},{4,None},{10,More}};
const NativeDacDriver::Step NativeDacDriver::endProgram_[]={{10,None},{10,None},{4,None},{7,ClearVoice},{10,Done}};
NativeDacDriver::Program NativeDacDriver::makeProgram(const Step *steps,unsigned size){
    Program p{steps,size,0,0,0};
    for(unsigned i=0;i<size;i++){
        if(steps[i].action==Address)p.addressAt=p.fixedClocks;
        if(steps[i].action==Data)p.dataAt=p.fixedClocks;
        p.fixedClocks+=steps[i].clocks;
    }
    return p;
}
const NativeDacDriver::Program NativeDacDriver::programs_[9]={
    makeProgram(highProgram_,sizeof(highProgram_)/sizeof(highProgram_[0])),
    makeProgram(highNormalProgram_,sizeof(highNormalProgram_)/sizeof(highNormalProgram_[0])),
    makeProgram(lowProgram_,sizeof(lowProgram_)/sizeof(lowProgram_[0])),
    makeProgram(lowNormalProgram_,sizeof(lowNormalProgram_)/sizeof(lowNormalProgram_[0])),
    makeProgram(zeroSetupProgram_,sizeof(zeroSetupProgram_)/sizeof(zeroSetupProgram_[0])),
    makeProgram(zeroLoopProgram_,sizeof(zeroLoopProgram_)/sizeof(zeroLoopProgram_[0])),
    makeProgram(highTailProgram_,sizeof(highTailProgram_)/sizeof(highTailProgram_[0])),
    makeProgram(finishByteProgram_,sizeof(finishByteProgram_)/sizeof(finishByteProgram_[0])),
    makeProgram(endProgram_,sizeof(endProgram_)/sizeof(endProgram_[0])),
};

int NativeDacDriver::advance(int clocks){
    int elapsed=0;
    while(active_ && elapsed<clocks){
        // Complete an uninterrupted normal nibble directly. At short deadlines
        // the instruction-boundary path below retains every intermediate state.
        const unsigned delayAddress=uint16_t(descriptor_+4);
        const bool stableDelay=delayAddress>=0x8000 ||
            (delayAddress<0x4000 && (delayAddress&8191)!=0x2e && (delayAddress&8191)!=0x1ffd);
        if(!step_ && (phase_==HighNormal || phase_==LowNormal) && stableDelay && clocks-elapsed>=256){
            unsigned delay=read_(context_,delayAddress);
            const auto &program=programs_[phase_];
            // HighNormal ends by reading the low nibble from the same byte;
            // reads of the 68000 bus (bank window) wait 3 clocks.
            unsigned duration=program.fixedClocks+13*(delay?delay:256)-5+(phase_==HighNormal&&pointer_>=0x8000?3:0)+
                              (delayAddress>=0x8000?3:0);
            // A DMA window within the nibble delays a read: take the steps.
            const uint64_t start=clockBase+unsigned(elapsed);
            const bool blocked=blockedUntil>start && blockedFrom<start+duration;
            if(!blocked && unsigned(clocks-elapsed)>=duration){
                delta_=ram_[0x1e + nibble_];ram_[0x2e]=delta_;accumulator_+=delta_;
                ram_[0x1ffd]=0x80;delay_=delay;
                const unsigned wait=delayAddress>=0x8000?3:0;   // the delay read precedes both writes
                eventOffset=elapsed+program.addressAt+wait;write_(context_,0x4000,0x2a);
                eventOffset=elapsed+program.dataAt+wait;write_(context_,0x4001,accumulator_);samples++;
                delay_=0;ram_[0x1ffd]=0;
                if(phase_==HighNormal){low_=true;nibble_=read_(context_,pointer_)&15;phase(Low);}
                else phase(FinishByte);
                elapsed+=duration;continue;
            }
        }
        eventOffset=elapsed;
        auto oldPhase=phase_;Step s=steps_[step_++];int cost=s.clocks;
        // Group arithmetic-only timing steps while retaining the exact final
        // instruction boundary. No device/RAM event may be crossed here.
        if(s.action==None){
            elapsed+=cost;
            while(elapsed<clocks && step_<size_ && steps_[step_].action==None)
                elapsed+=steps_[step_++].clocks;
            if(step_==size_ && phase_==ZeroSetup)phase(ZeroLoop);
            continue;
        }
        switch(s.action){
        case None:break;
        case ReadHigh:low_=false;if(pointer_>=0x8000)cost+=busRead(elapsed);nibble_=read_(context_,pointer_)>>4;break;
        case ReadLow:low_=true;if(pointer_>=0x8000)cost+=busRead(elapsed);nibble_=read_(context_,pointer_)&15;phase(Low);break;
        case Choose:phase(nibble_?(low_?LowNormal:HighNormal):ZeroSetup);break;
        case Delta:delta_=ram_[nibble_?0x1e + nibble_:0x2e];break;
        case StoreDelta:ram_[0x2e]=delta_;break;
        case Accumulate:accumulator_+=delta_;break;
        case BusyOn:ram_[0x1ffd]=0x80;break;
        case LoadDelay:if(uint16_t(descriptor_+4)>=0x8000)cost+=busRead(elapsed);delay_=read_(context_,uint16_t(descriptor_+4));break;
        case Address:write_(context_,0x4000,0x2a);break;
        case Data:write_(context_,0x4001,accumulator_);samples++;break;
        case Delay:{
            unsigned count=delay_?unsigned(delay_):256;
            unsigned taken=std::min(count-1,unsigned(clocks-elapsed-1)/13+1);
            if(taken){delay_-=taken;cost=13*taken;step_--;}
            else {delay_=0;cost=8;}
            break;
        }
        case BusyOff:ram_[0x1ffd]=0;break;
        case RepeatCount:repeat_=ram_[0x1e];break;
        case Call:ram_[(stack_-1)&8191]=low_?1:0;ram_[(stack_-2)&8191]=low_?0x35:0xee;break;
        case RepeatDec:--repeat_;break;
        case RepeatDone:if(!repeat_)phase(low_?FinishByte:HighTail);break;
        case RepeatAgain:phase(ZeroLoop);break;
        case Command:interrupt_=ram_[0x1fff]&128;break;
        case Interrupt:if(interrupt_)active_=false;break;
        case NextByte:pointer_++;break;
        case CountByte:remaining_--;break;
        case More:phase(remaining_?High:End);break;
        case ClearVoice:ram_[0x1ff6]=0;break;
        case Done:active_=false;break;
        }
        elapsed+=cost;
        if(phase_==oldPhase && step_==size_){
            if(phase_==ZeroSetup)phase(ZeroLoop);
            else if(phase_==LowNormal)phase(FinishByte);
        }
    }
    return elapsed;
}
