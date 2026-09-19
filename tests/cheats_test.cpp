#include "cheats.hpp"
#include "SystemMemory.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

using sor::cheats::Menu;
static void tap(Menu &menu, bool PlayerControlsState::*button) {
    PlayerControlsState p{}; p.connected = true;
    menu.input(p); p.*button = true; menu.input(p); p.*button = false; menu.input(p);
}
static void down(Menu &menu) { tap(menu, &PlayerControlsState::down); }
static void right(Menu &menu) { tap(menu, &PlayerControlsState::right); }
static SystemMemory memory;
int main() {
    Menu menu;
    memory.writeWord(0xffff02, 5); memory.writeWord(0xffff04, 7);
    menu.prepareNewGame(memory);
    assert(memory.readWord(0xffff02) == 5 && memory.readWord(0xffff04) == 7);
    assert(menu.startingLives(7) == 7); // Original hidden options still work.
    tap(menu, &PlayerControlsState::mode);
    assert(menu.visible() && menu.row() == 0);
    tap(menu, &PlayerControlsState::left);
    assert(menu.settings().round == 8);
    for (unsigned round = 1; round <= 8; ++round) {
        right(menu); assert(menu.settings().round == round);
        menu.prepareNewGame(memory);
        assert(memory.readWord(0xffff02) == round - 1 && memory.readWord(0xffff04) == 0);
    }
    down(menu);
    for (unsigned i = 0; i < 9; ++i) {
        right(menu);
        assert(menu.startingLives(7) == (i + 3) % 9 + 1);
    }
    PlayerControlsState held{}; held.connected = held.right = true;
    menu.input(held); const auto lives = menu.settings().lives;
    for (int i = 0; i < 60; ++i) menu.input(held);
    assert(menu.settings().lives == lives); // A held button is only one press.
    down(menu); right(menu); // Infinite lives.
    down(menu); right(menu); // Infinite health.
    down(menu); right(menu); // Infinite specials.
    memory = {};
    memory.writeWord(0xffff00, 0x16); memory.writeByte(0xffff18, 3);
    memory.writeByte(0xffb800, 1); memory.writeByte(0xffb880, 1);
    memory.writeWord(0xffff02, 2); memory.writeWord(0xffff04, 6);
    menu.apply(memory);
    assert(memory.readByte(0xffff20) == 9 && memory.readByte(0xffff23) == 9);
    assert(memory.readByte(0xffff21) == 1 && memory.readByte(0xffff24) == 1);
    assert(memory.readWord(0xffff02) == 2 && memory.readWord(0xffff04) == 6); // No mid-round warp.
    assert(menu.protectsHealth(0xffffb800, memory) && menu.protectsHealth(0xffb880, memory));
    assert(!menu.protectsHealth(0xffb900, memory));
    for (unsigned mode : {0x0au, 0x12u, 0x22u}) {
        memory.writeWord(0xffff00, mode); memory.writeByte(0xffff20, 0);
        menu.apply(memory); assert(memory.readByte(0xffff20) == 0);
        assert(!menu.protectsHealth(0xffb800, memory));
    }
    memory.writeWord(0xffff00, 0x16); memory.writeByte(0xffff34, 1);
    menu.apply(memory); assert(memory.readByte(0xffff20) == 0);
    assert(!menu.protectsHealth(0xffb800, memory));
    memory.writeByte(0xffff34, 0); memory.writeByte(0xffb880, 0); memory.writeByte(0xffff23, 0);
    menu.apply(memory); assert(memory.readByte(0xffff23) == 0); // Never resurrect absent P2.
    memory.writeWord(0xffff02, 7); memory.writeByte(0xffff21, 0);
    menu.apply(memory); assert(memory.readByte(0xffff21) == 0); // No police in Round 8.
    down(menu); tap(menu, &PlayerControlsState::c); // Restore defaults.
    assert(menu.settings().round == 1 && menu.settings().lives == 3);
    assert(!menu.settings().infiniteLives && !menu.settings().infiniteHealth && !menu.settings().infiniteSpecials);
    uint8_t before[65536]; std::memcpy(before, memory.state.ram, sizeof(before));
    menu.apply(memory); assert(!std::memcmp(before, memory.state.ram, sizeof(before)));
    tap(menu, &PlayerControlsState::x); assert(!menu.visible());
    puts("Cheats: round/lives ranges, edge input, both players, defaults, demos and Round 8 pass");
}
