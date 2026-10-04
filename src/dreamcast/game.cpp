#include <kos.h>
#include "SoR.hpp"
#include <exception>
#include <memory>
#include "replay.hpp"
#include "diagnostics.hpp"
extern "C" int sor_arithmetic_selftest();
KOS_INIT_FLAGS(INIT_DEFAULT);
int main(){
    vid_set_mode(DM_640x480,PM_RGB565);
    // Opaque and punch-through lists draw the game; the translucent list only the
    // shadows and pools of light of dynamic lighting (enhanced graphics).
    pvr_init_params_t params={{PVR_BINSIZE_16,PVR_BINSIZE_0,PVR_BINSIZE_16,PVR_BINSIZE_0,PVR_BINSIZE_16},1024*1024,0,0,0,0,0};
    // Thousands of small tile quads can exhaust a tile's initial object bin.
    // Match KOS's overflow reserve: without it retail PVR drops geometry in
    // rectangular blocks, even when the vertex buffer still has room.
    params.opb_overflow_count=3;
    pvr_init(&params);
    printf("PVR config: vertex_bytes=%d opb_overflow=%d free_vram=%lu\n",
        params.vertex_buf_size,params.opb_overflow_count,(unsigned long)pvr_mem_available());
    printf("Translated arithmetic selftest: %d\n",sor_arithmetic_selftest());
    // dc-tool's host filesystem supports the same scripted runs as a disc.
    if(!replay_load())replay_load("/pc/REPLAY.BIN");
    try {auto game=std::make_unique<StreetsOfRage>("/cd/SOR.BIN");game->boot();}
    catch(const std::exception &e){sor_flush_log();printf("SOR stopped: %s\n",e.what());fflush(stdout);}
    for(;;)thd_sleep(1000);
}
