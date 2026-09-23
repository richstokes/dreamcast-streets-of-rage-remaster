#include "sor/memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned bus_width,calls;
static uint32_t bus(void *p,uint32_t a,unsigned w){(void)p;assert(a==0xc00004);bus_width=w;calls++;return 0x1234;}
static sor_memory memory;
int main(void){
    const uint8_t rom[]={0x89,0xab,0xcd,0xef};
    sor_memory_init(&memory,rom,sizeof(rom));
    assert(sor_memory_read(&memory,0,4)==0x89abcdefu);
    assert(sor_memory_read(&memory,1,2)==0xabcdu);
    assert(sor_memory_read(&memory,3,2)==0 && memory.faults==1);
    sor_memory_write(&memory,0xfffffffdu,4,0x89abcdefu);
    assert(memory.ram[65533]==0x89 && memory.ram[0]==0xef);
    assert(sor_memory_read(&memory,0x00fffffdu,4)==0x89abcdefu);
    sor_memory_write(&memory,0,4,0);assert(sor_memory_read(&memory,0,4)==0x89abcdefu);
    memory.read_device=bus;assert(sor_memory_read(&memory,0xc00004,2)==0x1234);
    assert(calls==1 && bus_width==2);
    puts("core: big-endian, address mirror, boundary rejection, bus widths passed");
    return 0;
}
