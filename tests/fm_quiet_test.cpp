#include "ymfm_opn.h"
#include "ymfm_opn.cpp" // Make template definitions visible for isolated operator tests.
#include <cassert>
#include <cstdio>
#include <vector>
using Engine=ymfm::fm_engine_base<ymfm::opna_registers>;
using Operator=ymfm::fm_operator<ymfm::opna_registers>;
static void setState(Operator *op,uint16_t attenuation){
    std::vector<uint8_t> bytes;ymfm::ymfm_saved_state save(bytes,true);
    uint32_t phase=0xfffffffa;ymfm::envelope_state state=ymfm::EG_RELEASE;uint8_t zero=0;
    save.save(phase);save.save(attenuation);save.save(state);save.save(zero);save.save(zero);save.save(zero);
    ymfm::ymfm_saved_state restore(bytes,false);op->save_restore(restore);op->prepare();
}
static std::vector<uint8_t> snapshot(Operator *op){std::vector<uint8_t> bytes;ymfm::ymfm_saved_state s(bytes,true);op->save_restore(s);return bytes;}
int main(){
    ymfm::ymfm_interface a,b;Engine scalar(a),bulk(b);unsigned cases=0;
    for(unsigned rr=0;rr<16;rr++)for(unsigned ksr=0;ksr<4;ksr++)for(unsigned freq:{0x800u,0x3fffu}){
        for(auto engine:{&scalar,&bulk}){
            engine->reset();engine->write(0x30,7);engine->write(0x50,ksr<<6);engine->write(0x80,rr);
            engine->write(0xa4,freq>>8);engine->write(0xa0,freq);engine->clock(Engine::ALL_CHANNELS);
        }
        auto x=scalar.debug_operator(0),y=bulk.debug_operator(0);
        for(uint16_t att:{uint16_t(896),uint16_t(1000),uint16_t(1023)})
        for(uint32_t start:{0u,0xfffffffeu,0xfffff000u})for(unsigned n:{1u,2u,3u,20u,4096u}){
            setState(x,att);setState(y,att);assert(y->sor_quiet());uint32_t env=start;
            for(unsigned i=0;i<n;i++){env++;if((env&3)==3)env++;x->clock(env,0);}
            y->sor_advance_quiet(n,start,env);
            assert(snapshot(x)==snapshot(y));cases++;
        }
    }
    printf("FM quiet advance: %u cases match scalar operator state across release rates and clock/phase wrap\n",cases);
}
