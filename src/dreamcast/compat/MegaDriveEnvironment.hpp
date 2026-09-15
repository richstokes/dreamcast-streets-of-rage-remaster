#pragma once
#include "SystemMemory.hpp"
#include "Controllers.hpp"
#include "VDPPort.hpp"
#include "VDPRenderer.hpp"
#include <cstdint>
#include <string>
/* Transitional SoR-specific device facade. No 68000 interpreter. Audio not yet integrated. */
class VDP: public VDPPort {public:
    enum Synchronization {VSync}; enum Scaling {Integer}; enum SpriteLimit {HardwareSpriteLimit};
    explicit VDP(VDPState &s):VDPPort(s){}
};
class SilentSound {public: void endFrame(){};};
class PendingZ80 {public:
    uint8_t ram[8192]{}; void setReset(bool){} void setBusRequest(bool){}
    bool busRequestAcked(){return true;}
    void writeRAMFor68K(uint16_t a,uint8_t v){ram[a&8191]=v;}
};
class MegaDriveEnvironment {
public:
    using OptionHotkeyCode=int;
    explicit MegaDriveEnvironment(VDP::Synchronization,VDP::Scaling,VDP::SpriteLimit,uint16_t);
    virtual ~MegaDriveEnvironment();
    void boot(){onPowerOn();run();}
    void loadROM(const std::string &);
    SystemMemory &memory(){return mem_;} VDP &vdp(){return port_;}
    Controllers &controllers(){return pads_;} PendingZ80 &z80(){return z80_;}
    SilentSound &sound(){return sound_;}
    bool shouldQuit()const{return quit_;}
    int irqLevel()const{return irq_;} void clearInterrupt(int){irq_=0;}
    void pace(); void waitForInterrupt();
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
    void present();
    SystemMemory mem_; VDPState state_; VDP port_; VDPTile tile_; Framebuffer fb_; VDPRenderer renderer_;
    Controllers pads_; PendingZ80 z80_; SilentSound sound_;
    uint8_t th_[2]{0x40,0x40}; uint8_t *rom_=nullptr; bool quit_=false; int irq_=0;
    uint64_t cycles_=0; uint32_t last_=0,frames_=0,paceCount_=0;
};
