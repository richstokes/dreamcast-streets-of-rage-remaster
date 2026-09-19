#include "SoR.hpp"
#include "SoRCheats.hpp"
#include "cheats.hpp"
StreetsOfRage::~StreetsOfRage(){if(callLog_)fclose(callLog_);}
void StreetsOfRage::setCallLog(const std::string &){}
void StreetsOfRage::logEntry(m_long){}
void StreetsOfRage::logCall(m_long,m_long,m_long){}
void StreetsOfRage::handleOptionHotkey(OptionHotkeyCode){}
void StreetsOfRage::dumpUnhandledDispatchCpuState(){printf("SSP=%08lx SR=%04x\n",(unsigned long)cpu().ssp,cpu().status());}
void applyPendingSoRCheats(SystemMemory &memory){sor::cheats::menu.apply(memory);}
