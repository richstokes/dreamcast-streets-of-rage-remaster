#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
// Deterministic sound-only compatibility boundary. No 68000/VDP emulation.
class NativeAudio {
public:
    explicit NativeAudio(bool enabled=true,bool nativeDac=true);~NativeAudio();
    const bool enabled,nativeDac;
    uint8_t ram[8192]{};
    void endFrame(){} // Rendering is driven by the explicit native frame boundary.
    void setROM(const uint8_t *,size_t);
    void setReset(bool);void setBusRequest(bool);bool busRequestAcked(){return true;}
    void writeRAMFor68K(uint16_t a,uint8_t v){ram[a&8191]=v;}
    void writeYM(unsigned port,uint8_t value);uint8_t readYM(unsigned port);
    void writePSG(uint8_t value);
    // 68000-side chip writes, stamped with master clocks since the current
    // frame began. They take effect at that sample of the next rendered block
    // (the driver's key-off, note setup and key-on are microseconds apart on
    // hardware, and the chip clocks between them). writeYM/writePSG apply
    // immediately and remain the Z80 path and the between-frame test path.
    void writeYM68k(unsigned port,uint8_t value,uint32_t clocks);
    void writePSG68k(uint8_t value,uint32_t clocks);
    unsigned renderFrame(int16_t *stereo,uint64_t (*clock)()=nullptr,int16_t *dacStereo=nullptr);
    // The Z80 runs in frame batches after the 68000. For the 68000's view of
    // the DAC driver's busy flag ($1FFD bit 7) during a frame, a shadow of the
    // driver advances from the frame's start with 68000 time, stalled while
    // the 68000 holds the Z80 bus. clocks: master clocks since the frame began.
    void beginFrame68k();
    void busRequest68k(bool held,uint32_t clocks);
    uint8_t dacBusy68k(uint32_t clocks);
    uint32_t fmWorkload=0;
    uint64_t profile[5]{}; // At most 890 stereo frames.
    uint64_t nativeDacSamples=0,nativeDacStarts=0;
    uint64_t batchFrames=0,interleavedFrames=0;
    uint64_t ymWrites=0,psgWrites=0,dacWrites=0,z80Faults=0;
    static constexpr unsigned sampleRate=53693175/7/144;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
