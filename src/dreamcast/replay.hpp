#pragma once
#include <cstdint>
#include "Controllers.hpp"
bool replay_load(const char *path="/cd/REPLAY.BIN");
bool replay_finished();
// A replay is playing: diagnostics stay deferred so they cannot distort it.
bool replay_active();
bool replay_poll(PlayersControlState &,const uint8_t *ram);
