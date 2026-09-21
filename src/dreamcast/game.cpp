#include <kos.h>
#include "SoR.hpp"
#include "SoRCheats.hpp"
#include <exception>
#include <memory>
#include "storage.hpp"
#include "replay.hpp"
#include "diagnostics.hpp"
extern "C" int sor_arithmetic_selftest();
KOS_INIT_FLAGS(INIT_DEFAULT);
int main(){
    vid_set_mode(DM_640x480,PM_RGB565);
    // Opaque and punch-through lists draw the game; the translucent list only the
    // shadows and pools of light of dynamic lighting (enhanced graphics).
    pvr_init_params_t params={{PVR_BINSIZE_16,PVR_BINSIZE_0,PVR_BINSIZE_16,PVR_BINSIZE_0,PVR_BINSIZE_16},1024*1024,0,0,0,0,0};
    pvr_init(&params);
    printf("Translated arithmetic selftest: %d\n",sor_arithmetic_selftest());
    replay_load();
    sor_settings settings{0,0,0,80,80};
    printf("VMU settings: %s\n",dc_load_settings(settings)==0?"loaded":"defaults (missing/invalid is safe)");
    try {auto game=std::make_unique<StreetsOfRage>("/cd/SOR.BIN");game->boot();}
    catch(const std::exception &e){sor_flush_log();printf("SOR stopped: %s\n",e.what());fflush(stdout);}
    for(;;)thd_sleep(1000);
}
