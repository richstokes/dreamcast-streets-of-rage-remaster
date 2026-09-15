#!/usr/bin/env python3
"""Generate executable probes from upstream semantics, independent expected results."""
from pathlib import Path
import sys
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'research/StreetsOfRageProject/RageDecompiler'))
from tools.recompiler import cpp_semantics as s
from tools.recompiler.ea_codegen import TempPool
out=['#include "CPU68K.hpp"','#include <cstdio>','#include <climits>',s.CAST_MACROS.split('#define BEFORE_INSTRUCTION')[0],
     'class Probe { public: CPU68K state; CPU68K &cpu(){return state;}']
for name,kind,size,typ in [('add8','add','b','m_byte'),('add32','add','l','m_long'),('sub16','sub','w','m_word')]:
 out += [f'{typ} {name}({typ} v,{typ} src) {{',*getattr(s,kind)('v','src',size,TempPool(0)), 'return v;}']
out += ['m_long asr32(m_long v,m_long count){',*s.shift('v','count','l','ASR',TempPool(0),count_may_be_zero=True),'return v;}']
lines,result=s.muldiv('v','src','DIVS',TempPool(0))
out += ['m_long divs(m_long v,m_word src){',*lines,f'return {result};}}','};',r'''
extern "C" int sor_arithmetic_selftest(){
 Probe p;
 for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){
  p.state.sr=0; auto v=p.add8(a,b);unsigned sum=a+b;
  bool overflow=((a<128)==(b<128)) && ((v<128)!=(a<128));
  if(v!=(sum&255)||p.state.flagC()!=(sum>255)||p.state.flagX()!=(sum>255)||p.state.flagV()!=overflow||p.state.flagZ()!=(v==0)||p.state.flagN()!=(v>=128))return 1;
 }
 p.state.sr=0;if(p.add32(0xffffffffu,1)!=0 || !p.state.flagC() || !p.state.flagZ())return 2;
 p.state.sr=0;if(p.sub16(0,1)!=65535 || !p.state.flagC() || !p.state.flagN())return 3;
 p.state.sr=0;if(p.asr32(0x80000000u,32)!=0xffffffffu || !p.state.flagC())return 4;
 p.state.sr=0;if(p.asr32(0x80000000u,63)!=0xffffffffu)return 5;
 p.state.sr=CPU68K::FlagX;if(p.asr32(0x12345678u,0)!=0x12345678u || !p.state.flagX())return 6;
 p.state.sr=0;if(p.divs(0x80000000u,0xffffu)!=0x80000000u || !p.state.flagV())return 7;
 p.state.sr=0;if(p.divs(0xfffffff9u,3)!=0xfffffffeu)return 8;
 p.state.d[0]=0xdeadbeefu;p.state.setDb(0,0x42);if(p.state.d[0]!=0xdeadbe42u)return 9;
 p.state.setDw(0,0x1234);if(p.state.d[0]!=0xdead1234u)return 10;
 puts("arithmetic: 65536 ADD.b cases, wrap, carry, shifts, DIVS overflow and subregisters passed");return 0;
}
#ifdef SOR_PROBE_MAIN
int main(){return sor_arithmetic_selftest();}
#endif
''']
dest=R/'build/native/upstream/arithmetic-probes.cpp';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('\n'.join(out)+'\n')
