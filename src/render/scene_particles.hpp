#pragma once
#include <cstddef>
#include <cstdint>
namespace sor {
// The HUD's band at the top of the screen, in lines: no particle and nothing of
// the weather is drawn over it.
constexpr int HUD_LINES=36;
// The particles' and the weather's random numbers (xorshift32), apart from the game's.
inline uint32_t xorshift32(uint32_t &state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
// Particles of dynamic lighting (scene_light.hpp; docs/REMASTER.md): embers
// rising from fire, sparks thrown by hits and by the bazooka's flame, its grey
// smoke, dust under feet that land, the pieces of a broken prop, rain on the ground.
// Drawing only: they live in the scene, advance once per sprite-table build of
// the game (its tick), use their own random numbers and never touch the game.
// x is kept in the world (screen x + camera), so that scrolling leaves them
// where they were; positions are in 1/16 of a pixel.
struct ParticleDraw {
    int16_t x,y;            // centre on screen, in half pixels (the enhanced picture's pixels)
    uint8_t radius;         // in half pixels
    uint8_t colour[3],alpha;
    bool additive;          // light (embers, sparks) or matter (smoke)
};
class Particles {
public:
    enum Kind : uint8_t {EMBER,SPARK,SMOKE,DUST,DEBRIS,SPLASH};
    static constexpr size_t MAX=96;
    void spawn(Kind,int screenX,int screenY,int camera,int vx,int vy,unsigned life);
    // Convenience emitters: all positions on screen, in pixels.
    void fire(int x,int y,int camera);                 // a flame standing at x, y (its foot)
    void fireball(int x,int y,int camera);             // the bazooka's flame (or a boss's breath) at x, y
    void rocket(int x,int y,int camera);               // the bazooka's grey smoke in flight
    void burst(int x,int y,int camera);                // a hit spark appearing
    void dust(int x,int y,int camera);                 // feet coming down on the ground at x, y
    void debris(int x,int y,int camera);               // a prop breaking up
    void splash(int x,int y,int camera);               // a raindrop on the ground
    void advance(unsigned ticks);
    // What to draw, for a camera. Returns the count written (at most MAX).
    size_t draw(int camera,int width,int height,ParticleDraw *out) const;
    bool alive() const {return count_!=0;}
    void clear(){count_=0;}
    uint32_t random(){return xorshift32(random_);}
private:
    struct Particle {int32_t x,y;int16_t vx,vy;uint8_t life,span;Kind kind;};
    Particle particles_[MAX];
    size_t count_=0;
    uint32_t random_=0x2545F491u;
    int range(int low,int high){return low+int(random()%unsigned(high-low+1));}
};
}
