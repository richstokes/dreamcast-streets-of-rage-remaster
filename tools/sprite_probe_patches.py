"""Sprite-probe hooks in the translated SAT builder (enhanced rendering).

build_sprite_attribute_table ($AE96) starts a build; emit_object_sprite_mapping
($AF46) resolves an object's frame mapping at $AFE2 (a0 object, a1 mapping,
d2/d3 biased screen anchor, a2 next SAT record, Z clear for a mirrored frame)
and stores the next free record at $B0B6. The hooks only record these values
(src/render/sprite_probe.hpp); they change no game state or emulated time.

The three sites live in whichever generated partition holds them, so
`patch_generated` is applied to every SoR-*.cpp and reports how many hooks it
placed; the caller checks that all HOOKS landed.
"""
from patching import replace_once

HOOKS = 3


def patch_generated(text):
    """Returns (patched text, number of hooks placed)."""
    placed = 0
    for old, new in (
        ('    // $00AE96 move.b #$0000, $00FA02\n',
         '    spriteProbeBuild();\n    // $00AE96 move.b #$0000, $00FA02\n'),
        ('''    // $00AFE2 adda.w d0, a1
    {
        BEFORE_INSTRUCTION
        cpu().a[1] = cpu().a[1] + SEX_W(cpu().dw(0));
    }
''', '''    // $00AFE2 adda.w d0, a1
    {
        BEFORE_INSTRUCTION
        cpu().a[1] = cpu().a[1] + SEX_W(cpu().dw(0));
    }
    spriteProbeObject(cpu().a[0], cpu().a[1], cpu().ne(), cpu().dw(2), cpu().dw(3), cpu().a[2]);
'''),
        ('    // $00B0B6 move.w a2, $00FB04\n',
         '    spriteProbeEnd(cpu().a[2]);\n    // $00B0B6 move.w a2, $00FB04\n'),
    ):
        if old in text:
            text = replace_once(text, old, new)
            placed += 1
    return text, placed
