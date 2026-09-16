#pragma once
#include <cstdint>
void dac_aica_init(unsigned rate);
void dac_aica_submit(const int16_t *fm,const int16_t *dac,unsigned frames);
void dac_aica_shutdown();
