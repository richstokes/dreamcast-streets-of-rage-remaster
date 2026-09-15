#include <kos.h>
#include "SoR.hpp"
#include "SoRCheats.hpp"
#include <exception>
KOS_INIT_FLAGS(INIT_DEFAULT);
StreetsOfRage::~StreetsOfRage(){if(callLog_)fclose(callLog_);}
void StreetsOfRage::setCallLog(const std::string &){}
void StreetsOfRage::logEntry(m_long){}
void StreetsOfRage::logCall(m_long,m_long,m_long){}
void StreetsOfRage::handleOptionHotkey(OptionHotkeyCode){}
void StreetsOfRage::dumpUnhandledDispatchCpuState(){printf("SSP=%08lx SR=%04x\n",(unsigned long)cpu().ssp,cpu().status());}
void applyPendingSoRCheats(SystemMemory &){} // Development cheats disabled on target.
int main(){
    vid_set_mode(DM_640x480,PM_RGB565);pvr_init_defaults();
    try {auto game=new StreetsOfRage("/cd/SOR.BIN");game->boot();delete game;}
    catch(const std::exception &e){printf("SOR stopped: %s\n",e.what());}
    for(;;)thd_sleep(1000);
}
