#pragma once
#include <cstdint>
#include "Controllers.hpp"
bool replay_load(const char *path="/cd/REPLAY.BIN");
bool replay_finished();
bool replay_poll(PlayersControlState &,const uint8_t *ram);
