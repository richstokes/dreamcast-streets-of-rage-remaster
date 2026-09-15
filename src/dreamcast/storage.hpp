#pragma once
extern "C" {
#include "sor/save.h"
}
// Returns 0 on success, -1 if no VMU or valid record. Defaults are caller-owned.
int dc_load_settings(sor_settings &);
int dc_save_settings(sor_settings &);
