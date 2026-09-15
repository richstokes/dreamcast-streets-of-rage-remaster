#include <kos.h>
#include "SoR.hpp"
#include "SoRCheats.hpp"
#include <exception>
#include "storage.hpp"
#include "replay.hpp"
extern "C" int sor_arithmetic_selftest();
KOS_INIT_FLAGS(INIT_DEFAULT);
StreetsOfRage::~StreetsOfRage(){if(callLog_)fclose(callLog_);}
void StreetsOfRage::setCallLog(const std::string &){}
void StreetsOfRage::logEntry(m_long){}
void StreetsOfRage::logCall(m_long,m_long,m_long){}
void StreetsOfRage::handleOptionHotkey(OptionHotkeyCode){}
void StreetsOfRage::dumpUnhandledDispatchCpuState(){printf("SSP=%08lx SR=%04x\n",(unsigned long)cpu().ssp,cpu().status());}
void applyPendingSoRCheats(SystemMemory &){} // Development cheats disabled on target.
static void *monitor(void *p){for(;;){thd_sleep(5000);static_cast<StreetsOfRage*>(p)->debugState();}return nullptr;}
int main(){
    vid_set_mode(DM_640x480,PM_RGB565);pvr_init_defaults();
    printf("Translated arithmetic selftest: %d\n",sor_arithmetic_selftest());
    replay_load();
    sor_settings settings{0,0,0,80,80};
    printf("VMU settings: %s\n",dc_load_settings(settings)==0?"loaded":"defaults (missing/invalid is safe)");
    try {auto game=new StreetsOfRage("/cd/SOR.BIN");thd_create(0,monitor,game);game->boot();delete game;}
    catch(const std::exception &e){printf("SOR stopped: %s\n",e.what());}
    for(;;)thd_sleep(1000);
}
