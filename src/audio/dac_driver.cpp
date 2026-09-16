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
int NativeDacDriver::advance(int clocks){
    static constexpr Step high[]={{7,ReadHigh},{7,None},{4,None},{10,Choose}};
    static constexpr Step low[]={{7,None},{4,None},{10,Choose}};
    static constexpr Step highNormal[]={
        {4,None},{4,None},{4,None},{4,None},{7,None},{4,None},{4,None},
        {7,Delta},{13,StoreDelta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{7,Data},{6,None},{0,Delay},{4,None},{13,BusyOff},
        {4,None},{7,ReadLow}};
    static constexpr Step lowNormal[]={
        {4,None},{4,None},{4,None},{4,None},{7,None},{4,None},{4,None},
        {7,Delta},{13,StoreDelta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{4,None},{7,Data},{6,None},{0,Delay},{13,BusyOff}};
    static constexpr Step zeroSetup[]={{13,RepeatCount},{4,None}};
    static constexpr Step zeroLoop[]={
        {4,None},{4,None},{4,None},{4,None},{4,None},{4,None},{4,None},{17,Call},
        {13,Delta},{4,Accumulate},{4,None},{7,None},{13,BusyOn},{19,LoadDelay},
        {12,None},{7,None},{10,Address},{6,None},{4,None},{7,Data},{6,None},{0,Delay},
        {13,BusyOff},{10,None},{4,None},{4,RepeatDec},{10,RepeatDone},
        {4,None},{4,None},{4,None},{4,None},{10,RepeatAgain}};
    static constexpr Step highTail[]={{4,None},{7,ReadLow}};
    static constexpr Step finishByte[]={{4,None},{12,Command},{10,Interrupt},{6,NextByte},{6,CountByte},{4,None},{4,None},{10,More}};
    static constexpr Step end[]={{10,None},{10,None},{4,None},{7,ClearVoice},{10,Done}};
    int elapsed=0;
    while(active_ && elapsed<clocks){
        const Step *steps=nullptr;unsigned size=0;
        switch(phase_){
#define SELECT(p,a) case p:steps=a;size=sizeof(a)/sizeof(a[0]);break
            SELECT(High,high);SELECT(Low,low);SELECT(HighNormal,highNormal);SELECT(LowNormal,lowNormal);
            SELECT(ZeroSetup,zeroSetup);SELECT(ZeroLoop,zeroLoop);SELECT(HighTail,highTail);SELECT(FinishByte,finishByte);SELECT(End,end);
#undef SELECT
        }
        auto oldPhase=phase_;Step s=steps[step_++];int cost=s.clocks;
        // Group arithmetic-only timing steps while retaining the exact final
        // instruction boundary. No device/RAM event may be crossed here.
        if(s.action==None){
            elapsed+=cost;
            while(elapsed<clocks && step_<size && steps[step_].action==None)
                elapsed+=steps[step_++].clocks;
            if(step_==size && phase_==ZeroSetup)phase(ZeroLoop);
            continue;
        }
        switch(s.action){
        case None:break;
        case ReadHigh:low_=false;nibble_=read_(context_,pointer_)>>4;break;
        case ReadLow:low_=true;nibble_=read_(context_,pointer_)&15;phase(Low);break;
        case Choose:phase(nibble_?(low_?LowNormal:HighNormal):ZeroSetup);break;
        case Delta:delta_=ram_[nibble_?0x1e + nibble_:0x2e];break;
        case StoreDelta:ram_[0x2e]=delta_;break;
        case Accumulate:accumulator_+=delta_;break;
        case BusyOn:ram_[0x1ffd]=0x80;break;
        case LoadDelay:delay_=read_(context_,uint16_t(descriptor_+4));break;
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
        if(phase_==oldPhase && step_==size){
            if(phase_==ZeroSetup)phase(ZeroLoop);
            else if(phase_==LowNormal)phase(FinishByte);
        }
    }
    return elapsed;
}
