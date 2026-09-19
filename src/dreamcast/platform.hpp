#pragma once
#include <cstdint>
#include "Controllers.hpp"
#include "Framebuffer.hpp"
#include "VDPRenderer.hpp"
#include "title_caption.hpp"
extern "C" {
#include "sor/memory.h"
}
const uint8_t *platform_embedded_rom(size_t &size);
struct PlatformMemoryStats { uint32_t heap_used, vram_free; };
void platform_poll_controllers(PlayersControlState &);
void platform_video_init();
void platform_video_shutdown();
void platform_video_present(const Framebuffer &,int width,int height);
bool platform_render_vdp(VDPState &,VDPRenderer &,const sor::TitleCaption &);
uint64_t platform_time_us();
PlatformMemoryStats platform_memory_stats();
// Called before presentation/input sampling at each synchronous VBlank wait.
void platform_observe_frame(uint32_t,const sor_memory &,const Framebuffer &);
// Durations of the preceding frame's synthesis and presentation, for slow-frame logs.
void platform_frame_parts(uint32_t synth_us,uint32_t present_us);

void platform_audio_init(unsigned sampleRate);
void platform_audio_submit(const int16_t *,unsigned frames,const int16_t *dac=nullptr);
void platform_audio_shutdown();
bool platform_audio_enabled();

bool platform_audio_native_dac();

bool platform_audio_split_dac();

void platform_audio_report();
bool platform_audio_profile();
