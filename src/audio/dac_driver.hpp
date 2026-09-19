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
    int eventOffset=0; // Instruction start within the current advance call.
    bool active()const{return active_;}
    // Z80 clock at the start of the current advance() call, and a window in
    // which reads of the 68000 bus wait for its end (68000-to-VDP DMA holds
    // the bus; Genesis Plus GX, memz80.c).
    uint64_t clockBase=0,blockedFrom=0,blockedUntil=0;
    void cancel(){active_=false;}
    uint64_t samples=0;
    static bool recognizes(const uint8_t *ram);
private:
    enum Phase:uint8_t {High,HighNormal,Low,LowNormal,ZeroSetup,ZeroLoop,HighTail,FinishByte,End};
    enum Action:uint8_t {None,ReadHigh,ReadLow,Choose,Delta,StoreDelta,Accumulate,BusyOn,LoadDelay,Address,Data,Delay,BusyOff,RepeatCount,Call,RepeatDec,RepeatDone,RepeatAgain,Command,Interrupt,NextByte,CountByte,More,ClearVoice,Done};
    struct Step {uint8_t clocks;Action action;};
    struct Program {const Step *steps;unsigned size;unsigned fixedClocks,addressAt,dataAt;};
    static Program makeProgram(const Step *steps,unsigned size);
    static const Program programs_[9];
    static const Step highProgram_[];
    static const Step lowProgram_[];
    static const Step highNormalProgram_[];
    static const Step lowNormalProgram_[];
    static const Step zeroSetupProgram_[];
    static const Step zeroLoopProgram_[];
    static const Step highTailProgram_[];
    static const Step finishByteProgram_[];
    static const Step endProgram_[];
    const Step *steps_=nullptr;unsigned size_=0;
    uint8_t *ram_;void *context_;Read read_;Write write_;
    bool active_=false,low_=false,interrupt_=false;
    Phase phase_=High;unsigned step_=0;
    uint16_t pointer_=0,remaining_=0,descriptor_=0,stack_=0;
    uint8_t accumulator_=0,nibble_=0,delta_=0,repeat_=0,delay_=0;
    void phase(Phase p){phase_=p;step_=0;steps_=programs_[p].steps;size_=programs_[p].size;}
    // Clocks a 68000-bus read at `elapsed` into advance() waits: the DMA
    // window's remainder plus the Z80's 3-clock access wait.
    unsigned busRead(int elapsed)const{
        const uint64_t z=clockBase+unsigned(elapsed);
        return 3+(z>=blockedFrom&&z<blockedUntil?unsigned(blockedUntil-z):0);
    }
};
