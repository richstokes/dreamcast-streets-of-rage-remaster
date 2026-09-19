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
    int irqLevel()const{return irq_;} void clearInterrupt(int){irq_=0;}
    // Emulated 68000 time: translated instructions charge their MC68000 cycles
    // (7 master clocks each). Crossing the next frame boundary is a VBlank;
    // an explicit wait idles the CPU until it.
    // DRAM refresh stalls the bus for 2 cycles every 128 (Genesis Plus GX:
    // checked at each instruction start).
    void pace(unsigned cpuCycles=4){
        if(cycles_>=refreshAt_){refreshAt_=cycles_+128*7;cycles_+=2*7;}
        cycles_+=cpuCycles*7;if(cycles_>=nextVblank_)paceInterrupt();
    }
    // The 68000 is halted during 68K-to-VDP DMA.
    void stallCpu(uint64_t masterClocks){cycles_+=masterClocks;pcHistogram(0xFFFFFE,unsigned(masterClocks/7));}
#ifdef SOR_PC_HISTOGRAM
    // Host analysis (SOR_PC_HISTOGRAM_FRAMES=first:last:path): CPU cycles per
    // ROM address, comparable with genesis_reference.py --profile.
    void pcHistogram(unsigned pc,unsigned cpuCycles);
#else
    void pcHistogram(unsigned,unsigned){}
#endif
    void waitForInterrupt(); void debugState();
    void traceEnter(m_long a){last_=a;} m_long lastFunction()const{return last_;}
    void reportUnhandledDispatch(m_long);
    void confirmSpeculative(m_long){}
    uint64_t current68KMasterCycles()const{return cycles_;} bool isPal50Hz()const{return false;}
protected:
    virtual void run()=0; virtual int cpuInterruptMask()const=0;
    virtual void onPowerOn()=0; virtual void handleOptionHotkey(OptionHotkeyCode){}
    virtual void dumpUnhandledDispatchCpuState(){}
private:
    static uint32_t readBus(void *,uint32_t,unsigned);
    static void writeBus(void *,uint32_t,unsigned,uint32_t);
    void present(); void paceInterrupt(); void frameBoundary();
    SystemMemory mem_; VDPState state_; VDP port_; VDPTile tile_; Framebuffer fb_; VDPRenderer renderer_;
    Controllers pads_; NativeAudio audio_;
    uint8_t th_[2]{0x40,0x40}; uint8_t *rom_=nullptr; bool quit_=false; int irq_=0;
    // NTSC master clocks per frame; VBlank begins at line 224 of 262 (3,420
    // clocks per line), so the VDP's raster counters agree with emulated time.
    static constexpr uint64_t frameClocks=896040,vblankStart=224*3420;
    uint64_t cycles_=0,frameCycles_=0,nextVblank_=vblankStart,refreshAt_=0,ymBusyUntil_=0; uint32_t last_=0,frames_=0;
};
