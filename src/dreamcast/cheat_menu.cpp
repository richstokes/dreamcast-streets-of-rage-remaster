#include "cheats.hpp"
#include "platform.hpp"
#include "replay.hpp"

namespace sor::cheats {
namespace {
bool hint = false, waitRelease = false;
// Original 5x7 pixel font, kept in source (no ROM-derived menu artwork).
constexpr uint8_t letters[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}, {0,4,4,31,4,4,0}, {0,4,4,0,4,4,0},
    {1,2,2,4,8,8,16}, {0,8,4,2,4,8,0}, {0,0,0,31,0,0,0}
};
void text(void *context, PutPixel put, int x, int y, const char *s,
          unsigned r, unsigned g, unsigned b, int scale = 1) {
    for (; *s; ++s, x += 6 * scale) {
        int glyph = *s >= 'A' && *s <= 'Z' ? *s - 'A' : *s >= '0' && *s <= '9' ? *s - '0' + 26 :
            *s == '+' ? 36 : *s == ':' ? 37 : *s == '/' ? 38 : *s == '>' ? 39 : *s == '-' ? 40 : -1;
        if (glyph < 0) continue;
        for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
            if (letters[glyph][row] & (16 >> col))
                for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx)
                    put(context, x + col * scale + dx, y + row * scale + dy, r, g, b);
    }
}
void pixel(void *context, int x, int y, unsigned r, unsigned g, unsigned b) {
    static_cast<Framebuffer *>(context)->setPixel(x, y, b, g, r);
}
void rect(Framebuffer &fb, int x, int y, int w, int h, unsigned r, unsigned g, unsigned b) {
    for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) pixel(&fb, xx, yy, r, g, b);
}
bool anyButton(const PlayerControlsState &p) {
    return p.up || p.down || p.left || p.right || p.a || p.b || p.c || p.x || p.start || p.mode;
}
void release(PlayerControlsState &p) { const bool connected = p.connected; p = {}; p.connected = connected; }
}

bool hintVisible() { return hint && !menu.visible(); }
void drawHint(void *context, PutPixel put) {
    text(context, put, 112, 201, "L + R : CHEATS", 0, 0, 0);
    text(context, put, 111, 200, "L + R : CHEATS", 7, 7, 4);
}

void draw(const Menu &model, Framebuffer &fb) {
    rect(fb, 0, 0, 320, 224, 0, 0, 1);
    rect(fb, 14, 13, 292, 164, 0, 1, 2);
    rect(fb, 14, 13, 3, 164, 7, 4, 1);
    text(&fb, pixel, 30, 23, "CHEATS", 7, 5, 1, 2);
    text(&fb, pixel, 192, 29, "SESSION ONLY", 4, 5, 6);
    const auto &s = model.settings();
    const char *labels[] = {"START ROUND", "STARTING LIVES", "INFINITE LIVES", "INFINITE HEALTH",
                            "INFINITE SPECIALS", "GRAPHICS", "ANIMATION", "LIGHTING",
                            "RESTORE DEFAULTS", "RETURN TO GAME"};
    const char round[] = {char('0' + s.round), ' ', '/', ' ', '8', 0};
    const char lives[] = {char('0' + s.lives), 0};
    const char *values[] = {round, lives, s.infiniteLives ? "ON" : "OFF", s.infiniteHealth ? "ON" : "OFF",
                           s.infiniteSpecials ? "ON" : "OFF", s.enhancedGraphics ? "ENHANCED" : "ORIGINAL",
                           s.smoothAnimation ? "SMOOTH" : "ORIGINAL", s.dynamicLighting ? "DYNAMIC" : "ORIGINAL", "", ""};
    for (unsigned row = 0; row < 10; ++row) {
        const int y = 48 + row * 13;
        if (model.row() == row) {
            rect(fb, 23, y - 3, 275, 12, 0, 3, 4);
            text(&fb, pixel, 28, y, ">", 7, 7, 7);
        }
        text(&fb, pixel, 40, y, labels[row], 7, 7, 7);
        text(&fb, pixel, row >= 5 && row <= 7 ? 243 : 247, y, values[row], 7, 6, 2);
    }
    const char *help[] = {"ROUND AND LIVES: NEXT NEW GAME", "ROUND AND LIVES: NEXT NEW GAME",
        "BOTH PLAYERS / REFILLS TO 9", "BOTH PLAYERS / FALLS STILL COUNT",
        "BOTH PLAYERS / ROUNDS 1-7 ONLY", "PRESENTATION ONLY / SAME GAME",
        "IN-BETWEEN POSES / ENHANCED GRAPHICS ONLY",
        "SHADOWS AND LIGHT / ENHANCED GRAPHICS ONLY",
        "A: RESET ALL CHEAT SETTINGS", "A: RETURN / SETTINGS APPLY"};
    text(&fb, pixel, 23, 183, help[model.row()], 5, 6, 7);
    text(&fb, pixel, 23, 199, "UP/DOWN: SELECT  LEFT/RIGHT: CHANGE", 5, 6, 7);
    text(&fb, pixel, 23, 211, "A: CHANGE  B/START: RETURN", 5, 6, 7);
}

void poll(PlayersControlState &pads, const uint8_t *ram) {
    hint = ram[0xff00] == 0 && ram[0xff01] == 0x12;
    if (waitRelease) {
        waitRelease = anyButton(pads.player1);
        menu.input({});
        release(pads.player1);
        return;
    }
    menu.input(pads.player1);
    if (!menu.visible()) return;
    // No game instructions, IRQs or RAM writes run inside this modal loop.
    // Game changes are applied later at the existing safe VBlank-wait hook.
    static Framebuffer frame;
    static const int16_t silence[889 * 2]{};
    do {
        draw(menu, frame);
        platform_audio_submit(silence, 889);
        platform_cheat_menu_present(frame);
        if (!replay_poll(pads, ram)) platform_poll_controllers(pads);
        menu.input(pads.player1);
    } while (menu.visible());
    waitRelease = true;
    release(pads.player1);
    release(pads.player2);
}
}
