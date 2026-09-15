#include "sor/save.h"
#include <string.h>
uint32_t sor_crc32(const void *p, size_t n) {
    const uint8_t *s = p; uint32_t c = 0xffffffffu;
    while (n--) { c ^= *s++; for (unsigned b=0;b<8;b++) c = (c >> 1) ^ (0xedb88320u & (0u-(c&1u))); }
    return ~c;
}
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
void sor_save_encode(uint8_t out[SOR_SAVE_BYTES], const sor_settings *s) {
    memset(out, 0, SOR_SAVE_BYTES); memcpy(out, "SORS", 4); out[4]=1;
    out[5]=s->presentation; out[6]=s->music_volume; out[7]=s->sfx_volume;
    put32(out+8,s->sequence); put32(out+12,s->high_score);
    put32(out+28,sor_crc32(out,28));
}
int sor_save_decode(sor_settings *s, const uint8_t *p, size_t n) {
    if(n!=SOR_SAVE_BYTES || memcmp(p,"SORS",4) || p[4]!=1 || p[5]>1 || p[6]>100 || p[7]>100 || get32(p+28)!=sor_crc32(p,28)) return -1;
    s->presentation=p[5]; s->music_volume=p[6]; s->sfx_volume=p[7];
    s->sequence=get32(p+8); s->high_score=get32(p+12); return 0;
}
