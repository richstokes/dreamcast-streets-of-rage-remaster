#include "SystemMemory.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
struct Bus {uint32_t hash=2166136261u;unsigned calls=0;};
static uint32_t busRead(void *context,uint32_t a,unsigned width){auto &b=*static_cast<Bus*>(context);b.hash=(b.hash^a^width)*16777619u;b.calls++;return a^0xa5a5a5a5u;}
static void busWrite(void *context,uint32_t a,unsigned width,uint32_t v){auto &b=*static_cast<Bus*>(context);b.hash=(b.hash^a^width^v)*16777619u;b.calls++;}
int main(){
    SystemMemory native; sor_memory oracle;std::array<uint8_t,512*1024> rom;
    for(unsigned i=0;i<rom.size();i++)rom[i]=i^(i>>8);
    sor_memory_init(&native.state,rom.data(),rom.size());sor_memory_init(&oracle,rom.data(),rom.size());
    Bus a,b;native.state.device=&a;oracle.device=&b;
    native.state.read_device=oracle.read_device=busRead;native.state.write_device=oracle.write_device=busWrite;
    uint32_t random=0x534834;
    for(unsigned i=0;i<300000;i++){
        random^=random<<13;random^=random>>17;random^=random<<5;
        const uint32_t addresses[]={0xffff0000u|(random&65535),random&0x7ffff,0x7fffeu +(random&7),
            0xfffffffd+(random&7),0xa04000+(random&3),0xc00000+(random&15),random};
        uint32_t address=addresses[i%7];unsigned width=1u<<(i%3);
        uint32_t value=width==1?native.readByte(address):width==2?native.readWord(address):native.readLong(address);
        uint32_t expected=sor_memory_read(&oracle,address,width);
        // Public byte/word methods truncate MMIO values to the requested width.
        if(width<4)expected&=(1u<<(width*8))-1;
        assert(value==expected);
        uint32_t written=random;if(width<4)written&=(1u<<(width*8))-1;
        if(width==1)native.writeByte(address,written);else if(width==2)native.writeWord(address,written);else native.writeLong(address,written);
        sor_memory_write(&oracle,address,width,written);
        assert(native.state.faults==oracle.faults && native.state.last_fault_address==oracle.last_fault_address);
        assert(a.calls==b.calls && a.hash==b.hash);
    }
    assert(!std::memcmp(native.state.ram,oracle.ram,65536));
    puts("Native memory: 300000 accesses match bus oracle, including widths, mirrors, wrap, odd offsets and faults");
}
