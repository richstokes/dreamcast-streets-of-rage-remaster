"""Native cheat hooks at game initialization and the original health/HUD update.

Defaults leave the original values and emulated instruction charges untouched.
Generated derivatives remain local; only these checked adaptations are tracked.
"""


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned cheat hook changed: ' + old[:80])
    return text.replace(old, new)


def patch_menu(text):
    text = '#include "cheats.hpp"\n' + text
    return replace_once(text,
        '    const m_word lives = static_cast<m_word>(memory().readWord(kLivesSetting) * 2u + 1u);',
        '    sor::cheats::menu.prepareNewGame(memory());\n'
        '    const m_word lives = sor::cheats::menu.startingLives(memory().readWord(kLivesSetting) * 2u + 1u);')


def patch_generated(text):
    if 'void StreetsOfRage::adjust_player_health(m_long entry_) {' not in text:
        return text
    # Patch the calculation, including its shared entry, before the original
    # clamp, flags and HUD upload. Lethal hits therefore never see zero health.
    text = '#include "cheats.hpp"\n' + text
    return replace_once(text,
        '        t1 = t1 + cpu().dw(7);\n        memory().writeWord(t0, t1);',
        '        t1 = sor::cheats::menu.protectsHealth(cpu().a[0], memory()) ? 0x50u : t1 + cpu().dw(7);\n'
        '        memory().writeWord(t0, t1);')
