#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace sor {
// Equality test for the renderer's per-frame VRAM/CRAM cache checks. newlib's
// memcmp compares bytes; SH-4 compares aligned words about eight times faster.
inline bool equal_bytes(const void *a,const void *b,size_t n){
    typedef uint32_t __attribute__((may_alias)) word;
    if(((uintptr_t(a)|uintptr_t(b))&3)!=0 || (n&15)!=0)return !std::memcmp(a,b,n);
    const word *x=static_cast<const word*>(a),*y=static_cast<const word*>(b);
    for(size_t i=0;i<n/4;i+=4)
        if(((x[i]^y[i])|(x[i+1]^y[i+1])|(x[i+2]^y[i+2])|(x[i+3]^y[i+3]))!=0)return false;
    return true;
}
}
