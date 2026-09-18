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
    // Called before every translated instruction; keep the common case inline.
    void pace(){cycles_+=28;if(++paceCount_>=32000)paceInterrupt();}
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
    void present(); void paceInterrupt();
    SystemMemory mem_; VDPState state_; VDP port_; VDPTile tile_; Framebuffer fb_; VDPRenderer renderer_;
    Controllers pads_; NativeAudio audio_;
    uint8_t th_[2]{0x40,0x40}; uint8_t *rom_=nullptr; bool quit_=false; int irq_=0;
    uint64_t cycles_=0,frameCycles_=0; uint32_t last_=0,frames_=0,paceCount_=0;
};
