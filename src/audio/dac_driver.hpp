#pragma once
#include <cstdint>
// SoR1 DPCM playback state machine. Command/header setup stays outside this
// class. Instruction-boundary timing is retained for the shared YM2612 bus.
class NativeDacDriver {
public:
    using Read=uint8_t(*)(void*,uint16_t);
    using Write=void(*)(void*,uint16_t,uint8_t);
    NativeDacDriver(uint8_t *ram,void *context,Read read,Write write):ram_(ram),context_(context),read_(read),write_(write){}
    void start(uint16_t data,uint16_t bytes,uint16_t descriptor,uint8_t accumulator,uint16_t stack);
    int advance(int clocks);
    bool active()const{return active_;}
    void cancel(){active_=false;}
    uint64_t samples=0;
    static bool recognizes(const uint8_t *ram);
private:
    enum Phase {High,HighNormal,Low,LowNormal,ZeroSetup,ZeroLoop,HighTail,FinishByte,End};
    enum Action {None,ReadHigh,ReadLow,Choose,Delta,StoreDelta,Accumulate,BusyOn,LoadDelay,Address,Data,Delay,BusyOff,RepeatCount,Call,RepeatDec,RepeatDone,RepeatAgain,Command,Interrupt,NextByte,CountByte,More,ClearVoice,Done};
    struct Step {uint8_t clocks;Action action;};
    uint8_t *ram_;void *context_;Read read_;Write write_;
    bool active_=false,low_=false,interrupt_=false;
    Phase phase_=High;unsigned step_=0;
    uint16_t pointer_=0,remaining_=0,descriptor_=0,stack_=0;
    uint8_t accumulator_=0,nibble_=0,delta_=0,repeat_=0,delay_=0;
    void phase(Phase p){phase_=p;step_=0;}
};
