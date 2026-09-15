#pragma once
#include "data_types.hpp"
extern "C" {
#include "sor/memory.h"
}
#include <functional>
class SystemMemory {
public:
    sor_memory state{};
    m_byte readByte(m_long a) {return sor_memory_read(&state,a,1);}
    m_word readWord(m_long a) {return sor_memory_read(&state,a,2);}
    m_long readLong(m_long a) {return sor_memory_read(&state,a,4);}
    void writeByte(m_long a,m_byte v) {sor_memory_write(&state,a,1,v);}
    void writeWord(m_long a,m_word v) {sor_memory_write(&state,a,2,v);}
    void writeLong(m_long a,m_long v) {sor_memory_write(&state,a,4,v);}
    void copyToBuffer(m_long a,void *p,int n) {for(int i=0;i<n;i++) static_cast<m_byte*>(p)[i]=readByte(a+i);}
    void writeFromBuffer(void *p,m_long a,int n) {for(int i=0;i<n;i++) writeByte(a+i,static_cast<m_byte*>(p)[i]);}
    void copyByte(m_long a,m_long b) {writeByte(b,readByte(a));}
    void copyWord(m_long a,m_long b) {writeWord(b,readWord(a));}
    void copyLong(m_long a,m_long b) {writeLong(b,readLong(a));}
    void copyBytes(m_long a,m_long b,int n) {for(int i=0;i<n;i++)copyByte(a+i,b+i);}
    m_byte waitForByteValue(m_long a,m_byte v,const std::function<bool()> &f) {while(readByte(a)!=v) if(!f())break;return readByte(a);}
};
