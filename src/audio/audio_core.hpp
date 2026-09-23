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
    // Advance the Z80 to the 68000's time (master clocks since the frame
    // began) before the 68000 touches the Z80 bus, its RAM or the bus/reset
    // lines: while the 68000 holds the bus the Z80 is stopped, as on hardware.
    // The driver's YM writes keep their sample positions within the block.
    void sync68k(uint32_t clocks);
    // 68000 master clocks lost to the Z80's reads of the 68000 bus (drum
    // samples) since the last call, and whether a sample is playing.
    uint32_t takeBusStall();
    bool dacPlaying()const;
    // A 68000-to-VDP DMA holds the 68000 bus over [from, to) (master clocks
    // since the frame began): Z80 reads of it wait until the end.
    void blockBus68k(uint32_t from,uint32_t to);
    // Host analysis: the current frame's start in master clocks since power-on
    // (SOR_YM_TIMES=first:last:path logs every YM2612 write with its time).
    uint64_t frameStart68k=0;
    // Host analysis: take the Z80's state from the reference (state-synchronised
    // comparisons). bank: 68000 address of the bank window; regs: PC SP AF BC
    // DE HL IX IY AF' BC' DE' HL'; misc: I R IFF1 IFF2 IM HALT.
    void loadZ80(const uint8_t *zram,uint32_t bank,bool reset,bool busRequest,const uint16_t *regs,const uint8_t *misc);
    void logTimedWrite(uint64_t clock,unsigned port,uint8_t value);
    uint32_t fmWorkload=0;
    uint64_t profile[5]{};
    uint64_t nativeDacSamples=0,nativeDacStarts=0;
    uint64_t batchFrames=0,interleavedFrames=0;
    uint64_t ymWrites=0,psgWrites=0,dacWrites=0,z80Faults=0;
    // NTSC master clock: 7 clocks per 68000 cycle, 144 per YM2612 sample (1008).
    static constexpr unsigned frameClocks=896040,sampleClocks=1008,sampleRate=53693175/7/144;
    // renderFrame() outputs 888 or 889 stereo frames (896040/1008 = 888.93).
    static constexpr unsigned maxFrameSamples=(frameClocks+sampleClocks-1)/sampleClocks;
private:
    unsigned renderBlock(int16_t *stereo,uint64_t (*clock)(),int16_t *dacStereo);
    struct Impl;std::unique_ptr<Impl> impl;
};
