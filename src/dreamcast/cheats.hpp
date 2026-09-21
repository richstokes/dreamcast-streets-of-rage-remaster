#pragma once
#include <cstdint>
#include "Controllers.hpp"

class SystemMemory;
class Framebuffer;

namespace sor::cheats {
struct Settings {
    unsigned round = 1, lives = 3;
    bool infiniteLives = false, infiniteHealth = false, infiniteSpecials = false;
    // Presentation only: the simulation is the same in both modes.
    bool enhancedGraphics = false;
};

// Session settings live outside game RAM so stage loads and attract mode cannot
// erase them. No override is applied until a starting option is changed.
class Menu {
public:
    const Settings &settings() const { return settings_; }
    bool visible() const { return visible_; }
    unsigned row() const { return row_; }
    void input(const PlayerControlsState &);
    void prepareNewGame(SystemMemory &) const;
    unsigned startingLives(unsigned original) const;
    void apply(SystemMemory &) const;
    bool protectsHealth(uint32_t object, SystemMemory &) const;
    void setEnhancedGraphics(bool on) { settings_.enhancedGraphics = on; }
    // Scripted host runs (SOR_CHEATS in the headless build): start at a round
    // with the infinite options on, to visit content no replay reaches.
    void setScripted(unsigned round) {
        settings_.round = round; overrideRound_ = true;
        settings_.infiniteLives = settings_.infiniteHealth = settings_.infiniteSpecials = true;
    }
private:
    Settings settings_{};
    bool overrideRound_ = false, overrideLives_ = false, visible_ = false;
    unsigned row_ = 0, previous_ = 0;
};

extern Menu menu;
void poll(PlayersControlState &, const uint8_t *ram);
void draw(const Menu &, Framebuffer &);
bool hintVisible();
// RGB components are in the game's 0..7 range. Shared by both renderers.
using PutPixel = void (*)(void *, int, int, unsigned, unsigned, unsigned);
void drawHint(void *context, PutPixel);
}
