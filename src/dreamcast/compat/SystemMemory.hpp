#pragma once
#include "data_types.hpp"
extern "C" {
#include "sor/memory.h"
}
#include <functional>
class SystemMemory {
public:
    sor_memory state{};
    // Width is a compile-time property of each generated access. Keep ordinary
    // RAM/ROM traffic native; device accesses retain the side-effecting bus path.
    template<unsigned Width> uint32_t read(m_long address) {
        static_assert(Width==1 || Width==2 || Width==4);
        uint32_t a=address&0xffffffu,value=0;
        if(a>=0xff0000u){
            for(unsigned i=0;i<Width;i++)value=(value<<8)|state.ram[(a+i)&65535u];
            return value;
        }
        if(a<0x400000u && a<state.rom_size && Width<=state.rom_size-a){
            for(unsigned i=0;i<Width;i++)value=(value<<8)|state.rom[a+i];
            return value;
        }
        return sor_memory_read(&state,a,Width);
    }
    template<unsigned Width> void write(m_long address,uint32_t value){
        static_assert(Width==1 || Width==2 || Width==4);
        uint32_t a=address&0xffffffu;
        if(a>=0xff0000u){
            for(unsigned i=0;i<Width;i++)state.ram[(a+i)&65535u]=value>>(8*(Width-1-i));
            return;
        }
        if(a<0x400000u)return;
        sor_memory_write(&state,a,Width,value);
    }
    m_byte readByte(m_long a) {return read<1>(a);}
    m_word readWord(m_long a) {return read<2>(a);}
    m_long readLong(m_long a) {return read<4>(a);}
    void writeByte(m_long a,m_byte v) {write<1>(a,v);}
    void writeWord(m_long a,m_word v) {write<2>(a,v);}
    void writeLong(m_long a,m_long v) {write<4>(a,v);}
    void copyToBuffer(m_long a,void *p,int n) {for(int i=0;i<n;i++) static_cast<m_byte*>(p)[i]=readByte(a+i);}
    void writeFromBuffer(void *p,m_long a,int n) {for(int i=0;i<n;i++) writeByte(a+i,static_cast<m_byte*>(p)[i]);}
    void copyByte(m_long a,m_long b) {writeByte(b,readByte(a));}
    void copyWord(m_long a,m_long b) {writeWord(b,readWord(a));}
    void copyLong(m_long a,m_long b) {writeLong(b,readLong(a));}
    m_byte waitForByteValue(m_long a,m_byte v,const std::function<bool()> &f) {while(readByte(a)!=v) if(!f())break;return readByte(a);}
};
