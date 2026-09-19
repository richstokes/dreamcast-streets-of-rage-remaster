"""Sprite-probe hooks in the translated SAT builder (enhanced rendering).

build_sprite_attribute_table ($AE96) starts a build; emit_object_sprite_mapping
($AF46) resolves an object's frame mapping at $AFE2 (a0 object, a1 mapping,
d2/d3 biased screen anchor, a2 next SAT record, Z clear for a mirrored frame)
and stores the next free record at $B0B6. The hooks only record these values
(src/render/sprite_probe.hpp); they change no game state or emulated time.
"""


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned sprite builder changed; review probe hook: ' + old[:60])
    return text.replace(old, new)


def patch_generated(text):
    if '    // $00AE96 move.b #$0000, $00FA02\n' in text:
        text = replace_once(text, '    // $00AE96 move.b #$0000, $00FA02\n',
                            '    spriteProbeBuild();\n    // $00AE96 move.b #$0000, $00FA02\n')
    if '    // $00AFE2 adda.w d0, a1\n' in text:
        text = replace_once(text, '''    // $00AFE2 adda.w d0, a1
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
''')
    if '    // $00B0B6 move.w a2, $00FB04\n' in text:
        text = replace_once(text, '    // $00B0B6 move.w a2, $00FB04\n',
                            '    spriteProbeEnd(cpu().a[2]);\n    // $00B0B6 move.w a2, $00FB04\n')
    return text
