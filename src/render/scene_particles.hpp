#pragma once
#include <cstddef>
#include <cstdint>
namespace sor {
// Particles of dynamic lighting (scene_light.hpp; docs/REMASTER.md): embers
// rising from fire, sparks thrown by hits and by the police's rocket, its smoke.
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
    enum Kind : uint8_t {EMBER,SPARK,SMOKE};
    static constexpr size_t MAX=96;
    void spawn(Kind,int screenX,int screenY,int camera,int vx,int vy,unsigned life);
    // Convenience emitters: all positions on screen, in pixels.
    void fire(int x,int y,int camera);                 // a flame standing at x, y (its foot)
    void fireball(int x,int y,int camera);             // a falling fireball at x, y
    void rocket(int x,int y,int camera);               // the rocket in flight
    void burst(int x,int y,int camera);                // a hit spark appearing
    void advance(unsigned ticks);
    // What to draw, for a camera. Returns the count written (at most MAX).
    size_t draw(int camera,int width,int height,ParticleDraw *out) const;
    bool alive() const {return count_!=0;}
    void clear(){count_=0;}
private:
    struct Particle {int32_t x,y;int16_t vx,vy;uint8_t life,span;Kind kind;};
    Particle particles_[MAX];
    size_t count_=0;
    uint32_t random_=0x2545F491u;
    uint32_t random();
    int range(int low,int high){return low+int(random()%unsigned(high-low+1));}
};
}
