#include "audio_core.hpp"
#include <array>
#include <vector>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv){
 assert(argc==3);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)),{});
 std::array<uint8_t,8192> initial;std::ifstream(argv[2],std::ios::binary).read((char*)initial.data(),initial.size());
 NativeAudio reference(true,false),native,split;NativeAudio *cores[]={&reference,&native,&split};
 for(auto core:cores){core->setROM(rom.data(),rom.size());std::memcpy(core->ram,initial.data(),8192);core->setReset(true);core->setReset(false);}
 uint32_t random=0x736f7231;int16_t a[1780],b[1780],fm[1780],dac[1780];
 for(unsigned frame=0;frame<1000;frame++){
  random^=random<<13;random^=random>>17;random^=random<<5;
  for(auto core:cores){
   if(frame%17==0)core->ram[0x1fff]=0x81+random%17;
   if(frame%113==0){core->setReset(true);core->setReset(false);}
   if(frame%3==0){
    core->setBusRequest(true);
    unsigned attempts=0;
    while(core->ram[0x1ffd]&128){assert(attempts++<128);core->setBusRequest(false);core->setBusRequest(true);}
    core->writeYM(2,0xb6);core->writeYM(3,(random&3)<<6);
    core->writeYM(0,0x2c);core->writeYM(1,random&8);
    core->setBusRequest(false);
   }
  }
  auto n=reference.renderFrame(a);assert(n==native.renderFrame(b));assert(n==split.renderFrame(fm,nullptr,dac));
  assert(!std::memcmp(a,b,n*4));
  for(unsigned i=0;i<n*2;i++)assert(int(fm[i])+dac[i]==a[i]);
  assert(!std::memcmp(reference.ram,native.ram,8192));assert(!std::memcmp(reference.ram,split.ram,8192));
 }
 assert(native.nativeDacSamples>1000&&split.nativeDacSamples==native.nativeDacSamples);
 puts("DAC integration: 1000 frames, commands/resets/BUSREQ/pan/low-bit; PCM, sound RAM and stem sums match");
}
