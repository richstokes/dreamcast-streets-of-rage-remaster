#pragma once
#include <cstdint>
#include <algorithm>
// Native versions of the DAC driver's polling/delay instructions. The fallback
// retains every other opcode. Applicable only to local RAM, zero wait states,
// and a CPU with interrupts disabled; no MMIO reads or writes are skipped.
inline int soundZ80Execute(suzukiplan::Z80 &cpu, const uint8_t *ram, int clocks) {
    auto &r=cpu.reg;
    if(r.IFF || (r.interrupt&0xc0) || cpu.wtc.fetch || cpu.wtc.fetchM || cpu.wtc.read || cpu.wtc.write)
        return cpu.execute(clocks);
    int elapsed=0;
    while(elapsed<clocks) {
        unsigned pc=r.PC;
        int cost=0;unsigned refresh=1;
        if(pc<8190) {
            switch(ram[pc]) {
            case 0x7e: { // LD A,(HL), only the sound RAM window.
                unsigned hl=(unsigned(r.pair.H)<<8)|r.pair.L;
                if(hl<0x4000){r.pair.A=ram[hl&8191];r.PC++;cost=7;}
                break;
            }
            case 0xfe: { // CP immediate, including undocumented operand X/Y flags.
                unsigned a=r.pair.A,b=ram[pc+1],result=(a-b)&255;
                r.pair.F=2|(result&128)|(result==0?64:0)|(b&40)|
                    ((a^b^result)&16)|(((a^b)&(a^result)&128)?4:0)|(a<b?1:0);
                r.PC+=2;cost=7;break;
            }
            case 0xda: // JP C,nn
            case 0xd2: { // JP NC,nn
                unsigned target=ram[pc+1]|(unsigned(ram[pc+2])<<8);
                bool taken=bool(r.pair.F&1)==(ram[pc]==0xda);
                r.PC=taken?target:pc+3;r.WZ=target;cost=10;break;
            }
            case 0x10: // Collapse taken DJNZ $ iterations, preserving deadline overshoot.
                if(ram[pc+1]==0xfe && r.pair.B!=1) {
                    unsigned taken=r.pair.B?unsigned(r.pair.B)-1:255;
                    refresh=std::min(taken,unsigned(clocks-elapsed+12)/13);
                    r.pair.B-=refresh;cost=13*refresh;
                }
                break;
            }
        }
        if(cost) {
            r.R=(r.R&128)|((r.R+refresh)&127);r.execEI=0;r.consumeClockCounter=0;
            elapsed+=cost;
        } else {
            // Keep unrecognized work batched. Single-instruction interpreter
            // calls cost more than the delay optimization saves during samples.
            elapsed+=cpu.execute(clocks-elapsed);
            break;
        }
    }
    return elapsed;
}

// Locked SoR driver's idle loop (caller must verify its code identity). Advance
// whole poll iterations arithmetically; preserve PC/flags/WZ/refresh and overshoot.
inline int soundZ80Idle(suzukiplan::Z80 &cpu,const uint8_t *ram,int clocks){
    auto &r=cpu.reg;unsigned old=r.PC==0x32?0:r.PC==0x33?1:r.PC==0x35?2:3;
    if(r.IFF || (r.interrupt&0xc0) || cpu.wtc.fetch || cpu.wtc.fetchM || cpu.wtc.read || cpu.wtc.write ||
       old==3 || ram[0x1fff]>=0x81 || r.pair.H!=0x1f || r.pair.L!=0xff || clocks<=0)
        return soundZ80Execute(cpu,ram,clocks);
    static constexpr unsigned offsets[]={0,7,14,24};
    unsigned end=offsets[old]+unsigned(clocks),rounds=end/24,part=end%24;
    unsigned boundary=part==0?0:part<=7?1:part<=14?2:3;
    unsigned instructions=rounds*3+boundary-old;
    if(instructions<3)return soundZ80Execute(cpu,ram,clocks);
    unsigned a=ram[0x1fff],b=0x81,result=(a-b)&255;
    r.pair.A=a;r.pair.F=2|(result&128)|(result==0?64:0)|(b&40)|
        ((a^b^result)&16)|(((a^b)&(a^result)&128)?4:0)|(a<b?1:0);
    r.PC=boundary==1?0x33:boundary==2?0x35:0x32;r.WZ=0x32;
    r.R=(r.R&128)|((r.R+instructions)&127);r.execEI=0;r.consumeClockCounter=0;
    return int(rounds*24+offsets[boundary]-offsets[old]);
}
