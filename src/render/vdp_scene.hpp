#pragma once
#include "art_catalog.hpp"
#include "scene_light.hpp"
#include "scene_particles.hpp"
#include "scene_weather.hpp"
#include "VDPState.hpp"
#include "VDPRenderer.hpp"
#include "sprite_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstddef>
namespace sor {
struct TileQuad {
    uint16_t tile;
    uint16_t x,y,w,h;
    uint8_t palette,depth,u0,v0,u1,v1;
};
// Enhanced rendering: a hardware sprite piece's 8x8 cell, drawn as its own
// quad, and an object drawn with replacement art. `order` is the sprite's
// position in the SAT's link order (0 is in front); `layer` its priority.
struct SpriteTile {
    uint16_t tile;
    int16_t x,y;
    uint8_t palette,layer,order;
    bool hflip,vflip;
    uint8_t shade[3]{255,255,255},glow[3]{0,0,0};   // lighting: the owning object's light (scale and added light)
};
struct ArtDraw {
    uint32_t frame;          // index into ArtCatalog::frames()
    int16_t x,y;             // the object's anchor on screen
    uint8_t layer,order;
    bool flip;
    ArtTint tint;            // follows the game's fades and flashes
    int16_t lineFrom,lineTo; // screen lines [from, to) not blanked by a sprite mask
    uint8_t type=0;          // object type
    // Lighting (scene_light.hpp): the tint per corner, fade or flash included,
    // and the shadow the object casts on the ground line.
    bool lit=false,shadow=false;
    CornerLight light;
    int16_t ground=0;
};
// Lighting: a pool of light on the ground under an emitter (added to the picture).
struct GlowDraw {
    int16_t x,y,radiusX,radiusY;
    uint8_t colour[3],strength;
};
class VdpScene {
public:
    static constexpr size_t MAX_QUADS=32768;
    TileQuad quads[MAX_QUADS];
    size_t count=0;
    alignas(32) uint16_t sprites[2][512*256]{};
    uint16_t colors[64]{},background=0;
    int width=320,height=224;
    bool build(VDPState &);
    bool buildCached(VDPState &);
    bool reused=false,planesReused=false;
    int spriteTop[2]{256,256},spriteBottom[2]{};
    static uint16_t rgb1555(unsigned r,unsigned g,unsigned b);
    // Enhanced mode: sprites become spriteTiles and artDraws; the two composited
    // layers are not built (their by-product, the VDP's sprite overflow and
    // collision status bits, is never read by the game). Objects with no art
    // in `art` keep their original pieces. VDP sprite limits per line do not
    // apply to the enhanced drawing.
    bool enhanced=false;
    const ArtCatalog *art=nullptr;
    // Smooth animation: when an object with art changes pose and the art has an
    // in-between for that change, the in-between is drawn for the first
    // INBETWEEN_TICKS sprite-table builds of the new pose. Drawing only.
    // The game holds walking poses 8-12 ticks but attack poses as few as 2-4,
    // and the hold is not known when a pose starts: 2 keeps attacks readable.
    bool smooth=false;
    static constexpr unsigned INBETWEEN_TICKS=2;
    // Dynamic lighting of the enhanced drawing: see scene_light.hpp.
    bool lighting=false;
    unsigned round=0;                            // 1-8 (0: unknown): the round's light and weather profiles
    // Weather (scene_weather.hpp): rain, wet ground, haze, mist, shafts and
    // lightning by the round's profile. With lighting only (it draws with the
    // lights and the wall line).
    bool weather=false;
    // On while the game runs its objects (a sprite-table build has been displayed);
    // the runtime keeps it off outside a round (the title, menus, cutscenes).
    bool weatherOn() const {return enhanced&&lighting&&weather&&weatherActive_;}
    const WeatherProfile &weatherProfile() const {return weather_profile(weatherOn()?round:0);}
    const Weather &weatherState() const {return weather_;}
    void weatherStrike(){weather_.strikeNow();}   // previews: lightning at the next build
    WeatherQuad weatherQuads[MAX_WEATHER_QUADS];
    size_t weatherQuadCount=0;
    // Haze on a backdrop line (0-255 of the fog's colour), and the wet ground's
    // reflection of art standing on it (its alpha at the feet; 0: none).
    int fogAt(int y,bool farPlane) const {return weather_fog(weatherProfile(),y,light_.wallLine(),farPlane);}
    int reflectAlpha() const {return weatherOn()?reflect_alpha(weatherProfile()):0;}
    // ... for an object `up` lines above the ground: fainter the higher.
    static constexpr int reflectAlpha(int alpha,int up){return alpha*(96-std::clamp(up,0,96))/96;}
    static constexpr int RIM_SHIFT=2;            // art pixels the rim light's copy of the art is moved towards the lights
    static constexpr int WALL_ABOVE_LANES=16;
    static constexpr unsigned CONTACT_ALPHA=110; // of 255, at the middle of the contact shadow
    // The contact shadow's width, of 256 of the art's, for an object `up` lines above the ground.
    static constexpr int contactScale(int up){return up<=0?150:up>=96?60:150-up*90/96;}
    static constexpr size_t MAX_GLOWS=64,MAX_EMITTERS=24;
    GlowDraw glows[MAX_GLOWS];
    size_t glowCount=0;
    ParticleDraw particleDraws[Particles::MAX];
    size_t particleCount=0;
    size_t planeEnd[2]{};                        // quads [0, planeEnd[0]) are plane B's, then plane A's, then the window's
    static constexpr size_t MAX_SPRITE_TILES=80*16;
    SpriteTile spriteTiles[MAX_SPRITE_TILES];
    size_t spriteTileCount=0;
    ArtDraw artDraws[SpriteBuild::MAX_OBJECTS];
    size_t artCount=0;
    // The art changed (another round's pages): the cached scene's art draws are stale.
    void invalidate(){cacheValid=false;}
    const SceneLight &sceneLight() const {return light_;}
private:
    VDPState previous;
    bool cacheValid=false;
    uint16_t spriteFlags=0;
    bool builtEnhanced_=false,builtLighting_=false,builtWeather_=false;
    unsigned builtRound_=0;
    Weather weather_;
    bool weatherActive_=false;
    uint8_t builtFlash_=0;           // the lightning the scene was lit by: another flash is another scene
    LightProfile flashProfile_;      // the round's profile under lightning
    int camera_=0;
    void weatherStep();
    SceneLight light_;
    Particles particles_;
    uint16_t sparkSlots_[16]{};        // hit sparks on screen at the last build: a new one bursts
    unsigned sparkSlotCount_=0;
    uint32_t particleSerial_=0;
    // What stood where at the last build: a landing raises dust, a prop that is gone has broken.
    struct Tracked{uint16_t slot;int16_t x,y,level;uint8_t type;};
    Tracked tracked_[SpriteBuild::MAX_OBJECTS];
    unsigned trackedCount_=0;
    void particlesStep(const VDPState &,const SpriteBuild *displayed);
    uint16_t lightColors_[64]{},lightBackground_=0;   // what light_ was built from
    bool lightValid_=false,lightCollected_=false;
    unsigned collectedRound_=0;
    unsigned lightAge_=0;
    // The round's ground level (object +$18 of whatever stands on the ground;
    // it differs between rounds): the value most shadow casters share, or one
    // a lone object has kept for a while (a jump changes it every tick).
    // farthestGround_: the farthest ground line anything has stood on (lanes are about 36 lines deep).
    int16_t groundLevel_=0,loneLevel_=0,farthestGround_=0;
    unsigned loneBuilds_=0;
    bool groundKnown_=false;
    // The pose each object with art was last drawn in.
    struct Pose{uint32_t set,mapping,from;uint16_t slot;uint8_t ticks;bool flip;};
    Pose poses_[SpriteBuild::MAX_OBJECTS];
    unsigned poseCount_=0;
    uint32_t poseSerial_=0;
    bool inbetween_=false;       // one is on screen: the next frame differs with the same VDP state
    bool buildImpl(VDPState &,bool keepPlanes);
    void spriteLayers(VDPState &);
    void enhancedSprites(const VDPState &);
    void plane(const VDPState &,int plane);
    void window(const VDPState &);
    void add(uint16_t entry,int x,int y,int w,int h,int px,int py,int lowDepth);
};
// CPU oracle for the GPU command stream. Used only by host tests.
void raster_scene(const VdpScene &,const VDPState &,uint16_t *out);
// Host preview of an enhanced scene at twice the resolution (640x448 for H40).
void raster_enhanced(const VdpScene &,const VDPState &,uint16_t *out,int pitch);
}
