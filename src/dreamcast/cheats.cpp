#include "cheats.hpp"
#include "SystemMemory.hpp"

namespace sor::cheats {
Menu menu;
namespace {
constexpr uint32_t gameState = 0xffff00, playerMode = 0xffff18;
unsigned buttons(const PlayerControlsState &p) {
    return p.up | p.down << 1 | p.left << 2 | p.right << 3 |
        p.c << 4 | p.x << 5 | p.start << 6 | p.mode << 7;
}
unsigned step(unsigned value, unsigned maximum, bool increment) {
    return increment ? (value == maximum ? 1 : value + 1) : (value == 1 ? maximum : value - 1);
}
bool playing(SystemMemory &memory) {
    // $FF34 marks the attract-mode demo, which also uses gameplay state $16.
    return memory.readWord(gameState) == 0x16 && memory.readByte(0xffff34) == 0;
}
bool activePlayer(SystemMemory &memory, unsigned player) {
    return (memory.readByte(playerMode) & (1u << player)) &&
        memory.readByte(0xffb800 + player * 0x80) == 1;
}
void refill(SystemMemory &memory, uint32_t address, unsigned count, uint32_t hud) {
    memory.writeByte(address, count);
    // The original Update_Lives ($4E14) stages each digit as two tiles in
    // $FF6020 for the next VBlank upload. Keep that display in sync with RAM,
    // including a player who has not attacked or died since enabling a cheat.
    const unsigned tile = 0x6c0 + count * 2;
    memory.writeWord(hud, tile);
    memory.writeWord(hud + 0x50, tile + 1);
}
}

void Menu::input(const PlayerControlsState &pad) {
    constexpr unsigned rows = 11;
    const unsigned held = buttons(pad), pressed = held & ~previous_;
    previous_ = held;
    if (!pad.connected) return;
    if (!visible_) {
        if (pressed & 128) { visible_ = true; row_ = 0; }
        return;
    }
    if (pressed & (32 | 64 | 128)) { visible_ = false; return; }
    if (pressed & 3) {
        row_ = (row_ + ((pressed & 1) ? rows - 1 : 1)) % rows;
        return;
    }
    const bool confirm = pressed & 16;
    if (!(pressed & 12) && !confirm) return;
    const bool increment = !(pressed & 4);
    switch (row_) {
    case 0: settings_.round = step(settings_.round, 8, increment); overrideRound_ = true; break;
    case 1: settings_.lives = step(settings_.lives, 9, increment); overrideLives_ = true; break;
    case 2: settings_.infiniteLives = !settings_.infiniteLives; break;
    case 3: settings_.infiniteHealth = !settings_.infiniteHealth; break;
    case 4: settings_.infiniteSpecials = !settings_.infiniteSpecials; break;
    case 5: settings_.enhancedGraphics = !settings_.enhancedGraphics; break;
    case 6: settings_.smoothAnimation = !settings_.smoothAnimation; break;
    case 7: settings_.dynamicLighting = !settings_.dynamicLighting; break;
    case 8: settings_.weather = !settings_.weather; break;
    case 9:
        if (confirm) { settings_ = {}; overrideRound_ = overrideLives_ = true; }
        break;
    case 10: if (confirm) visible_ = false; break;
    }
}

unsigned Menu::startingLives(unsigned original) const {
    return overrideLives_ ? settings_.lives : original;
}

void Menu::prepareNewGame(SystemMemory &memory) const {
    if (overrideRound_) {
        memory.writeWord(0xffff02, settings_.round - 1);
        memory.writeWord(0xffff04, 0);
    }
}

void Menu::apply(SystemMemory &memory) const {
    if (!playing(memory)) return;
    if (weakenEnemies_) {
        // Scripted host runs only (SOR_CHEATS). One hit point, never zero: an enemy written dead skips its death
        // sequence and stays on the field.
        for (uint32_t object = 0xffb900; object < 0xffc400; object += 0x80) {
            const unsigned type = memory.readByte(object), health = memory.readWord(object + 0x32);
            if (type < 0x20 || type >= 0x60) continue;
            if (health > 1 && health < 0x8000) memory.writeWord(object + 0x32, 1);
            // An open-loop script cannot line up with enemies: bring awake,
            // grounded ones into player 1's lane, where its punches land.
            // and within reach, on the side they are already on.
            if (memory.readByte(object + 0x30) && memory.readWord(object + 0x18) == memory.readWord(0xffb818)) {   // both on the ground
                const int player = int16_t(memory.readWord(0xffb810)), x = int16_t(memory.readWord(object + 0x10));
                memory.writeWord(object + 0x14, memory.readWord(0xffb814));
                const int camera = int16_t(memory.readWord(0xffe002));
                int target = x > player + 40 ? player + 40 : x < player - 40 ? player - 40 : x;
                if (target < camera + 16) target = player + 40;      // never off screen, out of reach
                if (target > camera + 304) target = player - 40;
                if (target != x) memory.writeWord(object + 0x10, uint16_t(target));
            }
        }
    }
    for (unsigned player = 0; player < 2; ++player) {
        if (!activePlayer(memory, player)) continue;
        const uint32_t lives = 0xffff20 + player * 3;
        const uint32_t hud = 0xff6020 + player * 0x34;
        // Only top up. Turning a cheat off leaves the remaining stock playable.
        if (settings_.infiniteLives && memory.readByte(lives) < 9)
            refill(memory, lives, 9, hud);
        // Round 8 intentionally has no police support. Preserve that restriction
        // and its indoor scene rather than dispatching the outdoor cutscene.
        if (settings_.infiniteSpecials && memory.readWord(0xffff02) < 7 && memory.readByte(lives + 1) == 0)
            refill(memory, lives + 1, 1, hud + (player ? -26 : 10));
    }
}

bool Menu::protectsHealth(uint32_t object, SystemMemory &memory) const {
    if (!settings_.infiniteHealth || !playing(memory)) return false;
    object &= 0xffffff;
    return (object == 0xffb800 && activePlayer(memory, 0)) ||
        (object == 0xffb880 && activePlayer(memory, 1));
}
}
