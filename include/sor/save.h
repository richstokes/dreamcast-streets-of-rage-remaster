#ifndef SOR_SAVE_H
#define SOR_SAVE_H
#include <stddef.h>
#include <stdint.h>
enum { SOR_SAVE_BYTES = 32 };
typedef struct { uint32_t sequence, high_score; uint8_t presentation, music_volume, sfx_volume; } sor_settings;
uint32_t sor_crc32(const void *, size_t);
void sor_save_encode(uint8_t out[SOR_SAVE_BYTES], const sor_settings *);
int sor_save_decode(sor_settings *, const uint8_t *, size_t);
#endif
