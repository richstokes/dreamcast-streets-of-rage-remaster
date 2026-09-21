#pragma once
#include <cstdint>
class VDPState;
namespace sor {
// Which sprite-table records each game object emitted, recorded while the game
// builds its RAM SAT (build_sprite_attribute_table $AE96, per object in
// emit_object_sprite_mapping $AF46). Enhanced rendering uses it to draw an
// object's replacement art in place of its hardware sprite pieces. Recording
// changes nothing in the game or its timing.
struct ProbedObject {
    uint32_t mapping;       // ROM address of the frame's mapping record
    uint32_t set;           // object +$04: ROM address of its animation set
    uint16_t slot;          // object RAM address (low 16 bits)
    uint16_t tileBase;      // object +$0E: pattern base, palette and priority bits
    int16_t x,y;            // anchor in biased hardware coordinates (screen + 128)
    uint8_t type;           // object type
    uint8_t first,count;    // SAT records [first, first + count)
    bool flip;              // mirrored mapping (frame word bit 15)
};
struct SpriteBuild {
    static constexpr unsigned MAX_OBJECTS=80,MAX_RECORDS=80;
    ProbedObject objects[MAX_OBJECTS];
    unsigned count=0;
    uint8_t records[MAX_RECORDS*8]{};   // the RAM SAT as this build left it
    uint8_t emitted=0;                  // records written (HUD pieces included)
    uint32_t serial=0;
};
class SpriteProbe {
public:
    void beginBuild();
    void beginObject(uint16_t slot,uint8_t type,uint32_t mapping,bool flip,int16_t x,int16_t y,uint16_t tileBase,uint32_t sat,uint32_t set=0);
    void endObject(uint32_t sat,const uint8_t *ram);
    // The latest build whose records are the ones in VRAM's sprite table: the
    // RAM SAT reaches VRAM by DMA at the next graphics VBlank, so the build
    // being displayed can be the previous one. nullptr if neither matches.
    const SpriteBuild *displayed(const VDPState &) const;
private:
    SpriteBuild builds_[2];
    int current_=1;
    bool open_=false;
    ProbedObject pending_{};
    uint32_t start_=0,serial_=0;
};
SpriteProbe &sprite_probe();
}
