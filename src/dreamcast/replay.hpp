#pragma once
#include <cstdint>
#include "Controllers.hpp"
bool replay_load(const char *path="/cd/REPLAY.BIN");
uint32_t replay_total_frames();
bool replay_poll(PlayersControlState &);
