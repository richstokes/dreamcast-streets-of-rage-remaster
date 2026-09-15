#include <kos.h>
#include "SoR.hpp"
#include "SoRCheats.hpp"
#include <exception>
#include <memory>
#include "storage.hpp"
#include "replay.hpp"
extern "C" int sor_arithmetic_selftest();
KOS_INIT_FLAGS(INIT_DEFAULT);
int main(){
    vid_set_mode(DM_640x480,PM_RGB565);pvr_init_defaults();
    printf("Translated arithmetic selftest: %d\n",sor_arithmetic_selftest());
    replay_load();
    sor_settings settings{0,0,0,80,80};
    printf("VMU settings: %s\n",dc_load_settings(settings)==0?"loaded":"defaults (missing/invalid is safe)");
    try {auto game=std::make_unique<StreetsOfRage>("/cd/SOR.BIN");game->boot();}
    catch(const std::exception &e){printf("SOR stopped: %s\n",e.what());}
    for(;;)thd_sleep(1000);
}
