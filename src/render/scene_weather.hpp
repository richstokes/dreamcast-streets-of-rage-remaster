#pragma once
#include <cstddef>
#include <cstdint>
namespace sor {
// Weather (enhanced graphics with dynamic lighting; docs/REMASTER.md, "Weather"):
// rain, wet ground, haze, mist, light shafts and lightning, per round, all
// drawing only. The game has none of it: the atmosphere of a round is what its
// profile says, and every part of it is built from what the lighting already
// knows (the wall line, the lights, the art on the ground):
//
// - Rain: two sheets of streaks, one in front of everything and one behind the
//   characters (parallax), from a small tiling texture scrolled by time; the
//   particles' splashes on the ground.
// - Wet ground: the art again below the feet, flipped and fading; the wall's
//   lights smeared down the ground below them.
// - Haze: the backdrop's tiles take the fog's colour by their height (the far
//   plane more), as vertex colour and offset colour on the tile quads.
// - Mist: drifting sheets of soft noise at the ground and, faintly, in front.
// - Shafts and halos: the wall's lights throw a fan of light down to the ground
//   through the fog, and glow.
// - Lightning: a flash over everything, and for a few ticks the sky's shadow
//   takes over, leaning from the bolt.
//
// Integers throughout: the Dreamcast and the host preview draw a frame alike.
struct WeatherProfile {
    uint8_t rain;           // of 255: how much rain falls (0: none)
    uint8_t wet;            // of 255: how much the ground reflects
    uint8_t fog;            // of 255: haze over the backdrop, thickening towards the wall line
    uint8_t fogColour[3];
    uint8_t mist;           // of 255: drifting mist at the ground
    uint8_t shafts;         // of 255: the wall's lights' shafts and halos
    uint8_t lightning;      // of 255: how often lightning strikes (0: never)
    bool any() const {return rain||wet||fog||mist||shafts||lightning;}
};
const WeatherProfile &weather_profile(unsigned round);
// The two 64 x 64 alpha textures, generated once: streaks of rain, and soft
// tileable noise for mist. Both wrap.
constexpr int WEATHER_TEXTURE=64;
enum class WeatherTexture : uint8_t {NONE,STREAK,NOISE};
const uint8_t *weather_texture(WeatherTexture);
// A texture's alpha at (u, v) in 1/16 texel, bilinear, wrapping.
int weather_sample(WeatherTexture,int u16,int v16);
// The weather's own time and its lightning. A tick per sprite-table build of
// the game's, like the particles; its own random numbers.
class Weather {
public:
    void clear(){time_=0;flash_=0;hold_=0;second_=0;countdown_=0;lean_=0;}
    void advance(unsigned ticks,const WeatherProfile &);
    unsigned time() const {return time_;}
    int flash() const {return flash_;}     // 0-255: lightning's light now
    int lean() const {return lean_;}       // the bolt's shadow, as CastShadow::lean
    void strikeNow(){strike(255);}         // previews: lightning on demand
private:
    uint32_t time_=0;
    uint8_t flash_=0,hold_=0,second_=0;
    uint16_t countdown_=0;
    int8_t lean_=0;
    uint32_t random_=0x9E3779B9u;
    uint32_t random(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return random_;}
    void strike(int strength);
};
// A quad of weather to draw: corners top left, top right, bottom left, bottom
// right, in half pixels (the enhanced picture's), horizontal top and bottom
// edges; texture coordinates in texels (wrapping at WEATHER_TEXTURE); one
// colour, an alpha per corner; added to the picture or covering it.
struct WeatherQuad {
    enum Depth : uint8_t {BEHIND,GROUND,FRONT};   // between the planes; over the ground, under the sprites; over everything
    WeatherTexture texture;
    Depth depth;
    bool additive;
    int16_t x[4],y[4];
    int16_t u[4],v[4];
    uint8_t colour[3],alpha[4];
};
constexpr size_t MAX_WEATHER_QUADS=56;
// What the weather draws this frame, for a camera and a wall line: rain and
// mist sheets, the flash of lightning; with the lights (x, y, power, colour,
// low; count) the wet ground's smears and the fog's shafts.
struct WeatherLight {int16_t x,y;uint16_t power;uint8_t colour[3];bool low;};
size_t weather_quads(const WeatherProfile &,const Weather &,int camera,int width,int height,int horizon,
                     const WeatherLight *lights,unsigned lightCount,WeatherQuad *out);
// Haze: how much of the fog's colour a backdrop line takes, 0-255, for the far
// plane (B) or the near one (A), given the wall line.
int weather_fog(const WeatherProfile &,int y,int horizon,bool farPlane);
// The wet ground's reflection of art standing on it: its length in 1/64 of
// the art's height, and its alpha at the feet (of 255) for a profile.
constexpr int REFLECT_LENGTH=30;
inline int reflect_alpha(const WeatherProfile &p){return p.wet*185/255;}
constexpr uint8_t REFLECT_TINT[3]={150,160,180};
}
