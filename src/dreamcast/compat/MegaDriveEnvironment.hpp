#pragma once
#include "SystemMemory.hpp"
#include "Controllers.hpp"
#include "VDPPort.hpp"
#include "VDPRenderer.hpp"
#include <cstdint>
#include "audio_core.hpp"
#include <string>
/* Transitional SoR-specific device facade. No 68000 interpreter. Experimental sound-only compatibility is isolated in NativeAudio. */
class VDP: public VDPPort {public:
    enum Synchronization {VSync}; enum Scaling {Integer}; enum SpriteLimit {HardwareSpriteLimit};
    explicit VDP(VDPState &s):VDPPort(s){}
};
class MegaDriveEnvironment {
public:
    using OptionHotkeyCode=int;
    explicit MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t);
    virtual ~MegaDriveEnvironment();
    void boot(){onPowerOn();run();}
    void loadROM(const std::string &);
    SystemMemory &memory(){return mem_;} VDP &vdp(){return port_;}
    Controllers &controllers(){return pads_;} NativeAudio &z80(){return audio_;}
    NativeAudio &sound(){return audio_;}
    bool shouldQuit()const{return quit_;}
    // VINT stays pending from VBlank until acknowledged and interrupts the
    // 68000 (level 6) only while VDP register 1 enables it (IE0), as in
    // Genesis Plus GX; the game disables it during some loads.
    // Enabling IE0 with a word write while VINT is pending takes effect one
    // instruction later (Genesis Plus GX interrupt latency): irqHold_ lasts
    // until the next instruction is charged.
    int irqLevel()const{return vintPending_&&(state_.regs_[1]&0x20)&&!irqHold_?6:0;}
    // Acknowledge: the autovector exception takes 44 cycles.
    void clearInterrupt(int){vintPending_=false;pace(44);}
    // Emulated 68000 time. A translated instruction's MC68000 cycles are
    // charged when the next one starts, so its memory accesses happen at its
    // start as in Genesis Plus GX (Musashi adds an instruction's cycles after
    // executing it). At each boundary: settle the previous instruction, take a
    // VBlank crossed and any pending interrupt, then DRAM refresh (2 cycles
    // every 128, checked at each instruction start). An explicit wait idles
    // the CPU until the next VINT.
    void settleInstruction(){
        if(!pending_)return;
        cycles_+=uint64_t(pending_)*7;pending_=0;
        if(cycles_>=audioSyncAt_)syncAudio();
        if(cycles_>=nextVblank_)paceInterrupt();
    }
    void startInstruction(unsigned cpuCycles){
        irqHold_=false;
        if(cycles_>=refreshAt_){refreshAt_=cycles_+128*7;cycles_+=2*7;}
        pending_=cpuCycles;
    }
    // A data-dependent part of the current instruction (branch taken, MUL).
    void extendInstruction(unsigned cpuCycles){pending_+=cpuCycles;}
    // Hand-written code: spend cycles now (accesses that follow come after).
    void pace(unsigned cpuCycles=4){
        settleInstruction();
        irqHold_=false;
        if(cycles_>=audioSyncAt_)syncAudio();
        if(cycles_>=refreshAt_){refreshAt_=cycles_+128*7;cycles_+=2*7;}
        cycles_+=cpuCycles*7;if(cycles_>=nextVblank_)paceInterrupt();
    }
    // Time measured with DRAM refresh already included (decoder and profile
    // based charges for hand-written routines): no refresh is added here.
    void charge(unsigned cpuCycles){
        settleInstruction();
        cycles_+=uint64_t(cpuCycles)*7;
        if(cycles_>=audioSyncAt_)syncAudio();
        if(cycles_>=nextVblank_)paceInterrupt();
    }
    // Bring the Z80 up to the 68000's time and charge the 68000 the bus time
    // the Z80 took reading drum samples; while a sample plays, repeat every
    // 1,500 cycles so those stalls fall near where they happen.
    void syncAudio();
    // The 68000 is halted during 68K-to-VDP DMA.
    void stallCpu(uint64_t masterClocks){
        syncAudio();
        audio_.blockBus68k(uint32_t(cycles_-frameCycles_),uint32_t(cycles_-frameCycles_+masterClocks));
        cycles_+=masterClocks;pcHistogram(0xFFFFFE,unsigned(masterClocks/7));
    }
#ifdef SOR_PC_HISTOGRAM
    // Host analysis (SOR_PC_HISTOGRAM_FRAMES=first:last:path): CPU cycles per
    // ROM address, comparable with genesis_reference.py --profile.
    void pcHistogram(unsigned pc,unsigned cpuCycles);
#else
    void pcHistogram(unsigned,unsigned){}
#endif
    void waitForInterrupt(); void debugState();
#ifdef SOR_PC_HISTOGRAM
    // Host analysis: SOR_STATE_SYNC=state:replay replaces the machine state with
    // the reference's (genesis_reference.py --export-state) at the first VBlank
    // wait in play that matches the reference's (routine, stack and return
    // address), then plays `replay`.
    void stateSync();
#endif
#ifdef SOR_PC_HISTOGRAM
    // Host analysis (SOR_WATCH=pcs:last:path): time of each routine entry.
    void traceEnter(m_long a){last_=a;watchEnter(a);} void watchEnter(m_long a);
#else
    void traceEnter(m_long a){last_=a;}
#endif
    m_long lastFunction()const{return last_;}
    void reportUnhandledDispatch(m_long);
    void confirmSpeculative(m_long){}
    uint64_t current68KMasterCycles()const{return cycles_;} bool isPal50Hz()const{return false;}
protected:
    virtual void run()=0; virtual int cpuInterruptMask()const=0;
    virtual void onPowerOn()=0; virtual void handleOptionHotkey(OptionHotkeyCode){}
    virtual void dumpUnhandledDispatchCpuState(){}
    // D0-D7, A0-A6, SSP and SR: read (load=false) or replace (load=true).
    virtual void exchangeCpuState(uint32_t *regs,bool load){(void)regs;(void)load;}
private:
    static uint32_t readBus(void *,uint32_t,unsigned);
    static void writeBus(void *,uint32_t,unsigned,uint32_t);
    void present(); void paceInterrupt(); void frameBoundary();
    SystemMemory mem_; VDPState state_; VDP port_; VDPTile tile_; Framebuffer fb_; VDPRenderer renderer_;
    Controllers pads_; NativeAudio audio_;
    uint8_t th_[2]{0x40,0x40}; uint8_t *rom_=nullptr; bool quit_=false,vintPending_=false,irqHold_=false,longWrite_=false,idleToVblank_=false;
    // NTSC master clocks per frame; VBlank begins at line 224 of 262 (3,420
    // clocks per line), so the VDP's raster counters agree with emulated time.
    // VINT follows the VBlank flag by 788 clocks in H40 (770 in H32). At power
    // on the 68000 starts at line 191, 111,856 clocks before the first VBlank
    // (Genesis Plus GX, from the VDP's fixed power-on position).
    static constexpr uint64_t frameClocks=896040,vblankStart=224*3420,powerOn=vblankStart-111856;
    uint64_t vintDelay()const{return (state_.regs_[12]&1)?788:770;}
    uint64_t cycles_=powerOn,frameCycles_=0,vblankFlag_=vblankStart,nextVblank_=vblankStart+788,
             refreshAt_=(powerOn/896+1)*896,ymBusyUntil_=0,audioSyncAt_=~uint64_t(0); uint32_t last_=0,frames_=0,pending_=0;
};
