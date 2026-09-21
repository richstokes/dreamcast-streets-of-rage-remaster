#pragma once
#include "art_catalog.hpp"
#include <cstddef>
#include <cstdint>
class VDPState;
namespace sor {
struct TileQuad;
// Dynamic lighting (enhanced graphics; docs/REMASTER.md, "Lighting"). The game
// has no light: every sprite is drawn in its own colours wherever it stands,
// and nothing casts a shadow. Here the light comes from the picture itself:
//
// - The backdrop on screen (planes B and A, not the HUD's window) is reduced
//   to a grid of cells. Every cell above the horizon that outshines the screen
//   (shop windows, lamps, neon) is a light, standing in the wall behind the
//   playfield at its place and height, with its colour and power.
// - An object is lit by all of them at once, each by its distance over the
//   ground: the lights to its left and to its right tint that side's corners
//   of its art (brighter and in their colour), its feet take the ground's colour.
// - It casts a shadow away from each of the two groups of lights, from their
//   centre of power: longer the farther from the wall and the lower the
//   lights, swinging round as it walks past a shop window; and a contact
//   shadow under its feet. The windows' light spills onto the ground.
// - Objects that are light (fire, fireballs, hit sparks) add their colour to
//   what is near them, and to the ground.
//
// The PowerVR has no programmable shading. All of this is vertex colour
// (texture * colour + offset colour, interpolated over the quad) and two kinds
// of translucent quad (the art again, black, sheared onto the ground; an
// additive pool of light), which the host preview reproduces.
// Drawing only, from the VDP's state and the sprite probe: integers
// throughout, so the host and the Dreamcast light a frame alike.
struct LightSample {
    uint8_t colour[3];      // bright texels dominate
    uint8_t intensity;      // how much of the region outshines the screen's mean, 0-255
};
// A quad's corners: top left, top right, bottom left, bottom right.
struct CastShadow {
    int8_t lean=0;          // in 1/64 of the art's height: positive to the right
    uint8_t length=0;       // towards the viewer, in 1/64 of the art's height
    uint8_t alpha=0;        // 0: none
};
struct CornerLight {
    ArtTint corner[4];
    CastShadow shadow[2];   // away from the lights on the left, and on the right
    bool identity() const {return corner[0].identity()&&corner[1].identity()&&corner[2].identity()&&corner[3].identity();}
};
struct LightEmitter {
    int16_t x,y;            // centre on screen
    int16_t radius;
    uint8_t colour[3],strength;
};
class SceneLight {
public:
    static constexpr int CELL=16,COLS=20,ROWS=16;
    static constexpr unsigned SHADOW_ALPHA=150;  // of 255, shared between an object's shadows
    static constexpr int SPILL_DEPTH=44,SPILL_RISE=14;   // lines of ground the windows' light reaches; its soft upper edge
    // quads [0, endB) are plane B's, [endB, endA) plane A's.
    void build(const TileQuad *quads,size_t endB,size_t endA,const uint16_t *colors,uint16_t background,const VDPState &);
    LightSample gather(int x0,int y0,int x1,int y1) const;
    // The lights: cells above the horizon line that outshine the screen. Call after build().
    void collect(int horizon);
    struct Light {int16_t x,y;uint16_t power;uint8_t colour[3];};
    const Light *lights() const {return lights_;}
    unsigned lightCount() const {return lightCount_;}
    // Light spilt on the ground below the horizon, at each column boundary (strength 0-255).
    struct Spill {uint8_t colour[3],strength;};
    const Spill &spill(int boundary) const {return spill_[boundary];}
    int horizon() const {return horizon_;}
    // The light on an object whose art (or pieces) covers [x0,x1) x [y0,y1), standing on line `ground`.
    void shade(int x0,int y0,int x1,int y1,int ground,CornerLight &) const;
    // An emitter's light on the same box, added to the corners' offsets.
    static void glow(const LightEmitter &,int x0,int y0,int x1,int y1,CornerLight &);
    // A fade or flash of the game's applies to lit art too.
    static void apply(const ArtTint &,CornerLight &);
    uint8_t ambient[3]{255,255,255};
    const uint8_t *cell(int row,int column) const {return cell_[row][column];}
private:
    uint8_t cell_[ROWS][COLS][3]{};
    uint8_t bright_[ROWS][COLS][3]{};  // the cell's colour, its bright texels counting most
    uint16_t over_[ROWS][COLS]{};      // (the cell's RMS brightness over the screen's)^2
    static constexpr unsigned MAX_LIGHTS=80;
    Light lights_[MAX_LIGHTS];
    unsigned lightCount_=0;
    Spill spill_[COLS+1]{};
    int horizon_=0;
    int ambientLuminance_=0;
};
// Objects that are light, by type (object +$00) and frame, and the types that
// stand in the playfield: these are lit and cast shadows.
enum class LightKind : uint8_t {NONE,FIRE,FIREBALL,ROCKET,HIT_SPARK};
LightKind light_kind(unsigned type,uint32_t mapping);
inline bool emits_light(unsigned type,uint32_t mapping){const LightKind k=light_kind(type,mapping);return k!=LightKind::NONE&&k!=LightKind::ROCKET;}
bool in_playfield(unsigned type);
// An emitter's colour: the bright colours of its CRAM line (mask: entries its art uses).
void emitter_colour(const uint16_t *line,uint16_t mask,uint8_t rgb[3]);
}
