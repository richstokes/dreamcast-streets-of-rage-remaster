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
    unsigned renderFrame(int16_t *stereo,uint64_t (*clock)()=nullptr,int16_t *dacStereo=nullptr);
    uint64_t profile[3]{}; // At most 890 stereo frames.
    uint64_t nativeDacSamples=0,nativeDacStarts=0;
    uint64_t batchFrames=0,interleavedFrames=0;
    uint64_t ymWrites=0,psgWrites=0,dacWrites=0,z80Faults=0;
    static constexpr unsigned sampleRate=53693175/7/144;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
