#pragma once
#include <cstdint>
#include "Controllers.hpp"
#include "Framebuffer.hpp"
extern "C" {
#include "sor/memory.h"
}
struct PlatformMemoryStats { uint32_t heap_used, vram_free; };
void platform_poll_controllers(PlayersControlState &);
void platform_video_init();
void platform_video_shutdown();
void platform_video_present(const Framebuffer &,int width,int height);
uint64_t platform_time_us();
PlatformMemoryStats platform_memory_stats();
// Called before presentation/input sampling at each synchronous VBlank wait.
void platform_observe_frame(uint32_t,const sor_memory &,const Framebuffer &);
