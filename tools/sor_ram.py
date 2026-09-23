"""The game's work-RAM layout and routine names, as the comparison tools read them."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Object slots: the two players, then 66 slots of 128 bytes for everything else.
PLAYER1, PLAYER2 = 0xB800, 0xB880
OBJECT_SLOTS = (PLAYER1, PLAYER2, *range(0xB900, 0xDA00, 0x80))
MODE, STAGE, WAVE, CAMERA = 0xFF00, 0xFF02, 0xFF04, 0xE002
P1_LIVES, P2_LIVES = 0xFF20, 0xFF23
MODE_PLAYING = 0x16

# Byte ranges within an object that the comparisons report separately.
OBJECT_REGIONS = {'collision_ids': (2, 4), 'animation': (4, 14), 'positions_and_velocity': (16, 40),
                  'state_health_damage': (48, 53), 'grab_target': (76, 78), 'input': (84, 86),
                  'attack_flags': (88, 89), 'weapon': (94, 96), 'grab_mode_target': (125, 128),
                  'spawn_flags_and_timer': (73, 76), 'full_object': (0, 128)}


def word(ram, address):
    return (ram[address] << 8) | ram[address + 1]


def is_enemy(object_type):
    """Ordinary enemy ($20-$2F) or boss ($55-$58, Antonio at the end of Round 1)."""
    return 0x20 <= object_type <= 0x2F or 0x55 <= object_type <= 0x58


def gate_frame(directory, segment):
    """The frame at which a reference run passed the state gate of `segment`."""
    import json
    frames = [e['frame'] for e in json.loads((directory / 'events.json').read_text()) if e['segment'] == segment]
    if len(frames) != 1:
        raise ValueError(f'{directory}: expected one gate event for segment {segment}, found {len(frames)}')
    return frames[0]


def routine_labels():
    """Sorted (ROM address, name) pairs from the recompilation's label table."""
    rows = []
    path = ROOT / 'research/StreetsOfRageProject/StreetsOfRageRecompilation/code-analysis/labels.csv'
    for line in path.read_text().splitlines():
        if line.startswith('#') or ',' not in line:
            continue
        address, name = line.split(',', 2)[:2]
        try:
            rows.append((int(address, 16), name.strip()))
        except ValueError:
            pass
    return sorted(rows)
