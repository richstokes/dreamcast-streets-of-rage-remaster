#!/usr/bin/env python3
"""Play Round 1 in Genesis Plus GX with a scripted policy, as a reference run.

Recorded replays end where the player dies, so the boss and the end of the
round are beyond every comparison window. This plays the original with a simple
closed-loop policy (walk to the nearest awake enemy, line up its depth, punch)
and, until `--aids-until`, keeps the player alive and clears waves with RAM
writes. Those writes are an aid for reaching late content, so they stop before
the window that is compared: from `--aids-until` on, the run is the game
playing out the recorded inputs on its own.

It writes the inputs it chose as an ordinary scenario, and optionally the run
itself (`--output`: raw RAM captures and gate events, as genesis_reference.py
writes them) and a machine state (`--export-state`). tools/state-sync.py then
starts the native port from that state and replays the same inputs, so the two
backends can be compared on content no replay reaches from power-on.

  tools/bot-play.py CORE ROM PROLOGUE FRAMES OUT_SCENARIO [--weaken] [--output DIR]
"""
import argparse, ctypes, hashlib, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from genesis_reference import Genesis, BUTTONS, decoder_idle  # noqa: E402

PLAYER, HEALTH, CAMERA, LIVES = 0xB800, 0xB832, 0xE002, 0xFF20
SLOTS = [0xB880] + list(range(0xB900, 0xDA00, 0x80))


def word(ram, a): return (ram[a] << 8) | ram[a + 1]


def enemy(ram, s):
    """Ordinary enemy ($20-$2F) or boss ($55-$58, Antonio at the end of Round 1)."""
    return 0x20 <= ram[s] <= 0x2F or 0x55 <= ram[s] <= 0x58


def live(ram, s):
    """An enemy that is awake and not already dying: state $30 set, health sane.

    A slot keeps its type while the enemy waits off camera (state and health
    both zero) and reads health $FFFF while it dies."""
    return enemy(ram, s) and word(ram, s + 48) and word(ram, s + 50) < 0x8000


def policy(ram, frame, lure=False):
    """Buttons for this frame: close on the nearest live enemy, then punch.

    The camera stops while a wave is unfinished, so an enemy can stand beyond
    the edge the player is held at. `lure` backs away from it, which brings the
    enemy's own approach into range."""
    px, py = word(ram, PLAYER + 16), word(ram, PLAYER + 20)
    enemies = [(abs(word(ram, s + 16) - px) + abs(word(ram, s + 20) - py) * 2, s)
               for s in SLOTS if live(ram, s)]
    if not enemies: return ['RIGHT']                       # nothing awake: advance the camera
    _, slot = min(enemies)
    if lure: return ['LEFT'] if word(ram, slot + 16) > px else ['RIGHT']
    dx, dy = word(ram, slot + 16) - px, word(ram, slot + 20) - py
    buttons = []
    if abs(dy) > 6: buttons.append('DOWN' if dy > 0 else 'UP')
    if abs(dx) > 34: buttons.append('RIGHT' if dx > 0 else 'LEFT')
    if buttons: return buttons
    return ['B'] if frame % 6 < 2 else []                  # punch, with gaps for the combo


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('core'); ap.add_argument('rom'); ap.add_argument('prologue', type=Path)
    ap.add_argument('frames', type=int); ap.add_argument('out', type=Path)
    ap.add_argument('--health', type=int, default=80, help='health a standing player is topped up to while aids are on')
    ap.add_argument('--weaken', action='store_true', help='hold ordinary enemies (types $20-$27) at one hit point, so the '
                    'waves before the boss clear quickly; bosses are never touched')
    ap.add_argument('--stuck', type=int, default=400, help='frames without the camera moving before the policy backs off')
    ap.add_argument('--aids-until', type=int, default=None, metavar='FRAME',
                    help='play unaided from this frame on (default: aids stay on to the end)')
    ap.add_argument('--output', type=Path, help='write the run as a reference run (ram.bin, events.json, metadata.json)')
    ap.add_argument('--export-state', metavar='FRAME:PATH', help='machine state at the end of the first frame from FRAME '
                    'on that the native port can take (no incremental decode in flight); the frame used is reported')
    ap.add_argument('--verbose', action='store_true', help='report mode, wave, lives and camera as they change')
    a = ap.parse_args()
    g = Genesis(a.core, a.rom)
    export = a.export_state.split(':', 1) if a.export_state else None
    raw = None; events = []
    if a.output:
        a.output.mkdir(parents=True, exist_ok=True); raw = (a.output / 'ram.bin').open('wb')
    segments = []
    ram = g.ram()
    for index, segment in enumerate(json.loads(a.prologue.read_text())['segments']):
        masks = [sum(1 << BUTTONS[b] for b in segment.get(k, [])) for k in ('p1', 'p2')]
        gate = segment.get('wait')
        for elapsed in range(segment['frames'] + int(bool(gate))):
            if gate and ram[gate['address']] & gate.get('mask', 255) == gate['value']:
                events.append(dict(segment=index, frame=g.frame, idle_frames=elapsed)); break
            if gate and elapsed == segment['frames']: raise SystemExit('state gate timed out')
            ram = g.step(*masks)
            if raw: raw.write(ram)
        segments.append({k: v for k, v in segment.items() if k != 'capture'})
        if gate: break
    camera, held, reported = None, 0, None
    for frame in range(a.frames):
        held = held + 1 if word(ram, CAMERA) == camera else 0
        camera = word(ram, CAMERA)
        if a.aids_until is None or g.frame < a.aids_until:
            # Only top up a player who is still standing: the game's death
            # sequence waits for the empty health bar, and refilling it there
            # leaves the level pipeline waiting for a death that never happens.
            if 0 < word(ram, HEALTH) < a.health: g.poke(HEALTH, a.health, 2)
            if ram[LIVES] < 3: g.poke(LIVES, 3, 1)
            if a.weaken:
                for slot in SLOTS:
                    if live(ram, slot) and ram[slot] <= 0x27 and word(ram, slot + 50) > 1:
                        g.poke(slot + 50, 1, 2)
        buttons = policy(ram, frame, lure=held > a.stuck and held % (a.stuck * 2) < a.stuck // 2)
        if segments and segments[-1].get('p1', []) == buttons and 'wait' not in segments[-1]:
            segments[-1]['frames'] += 1
        else: segments.append(dict(frames=1, **({'p1': buttons} if buttons else {})))
        ram = g.step(sum(1 << BUTTONS[b] for b in buttons), 0)
        if raw: raw.write(ram)
        if export and g.frame >= int(export[0]) and decoder_idle(ram):
            g.lib.sor_export_state.argtypes = [ctypes.c_char_p]
            if g.lib.sor_export_state(export[1].encode()): raise SystemExit('state export failed')
            print('state exported at frame %d' % g.frame); export = None
        if a.verbose:
            now = (word(ram, 0xFF00), word(ram, 0xFF04), ram[0xFF20], word(ram, CAMERA) // 128,
                   tuple(sorted({ram[s] for s in SLOTS if enemy(ram, s)})))
            if now != reported:
                mode, wave, lives, camera128, types = now
                print('frame %6d  mode %04x wave %d lives %d camera %4d  objects %s' % (
                    g.frame, mode, wave, lives, camera128 * 128, ' '.join('%02x' % t for t in types)))
                reported = now
    a.out.write_text(json.dumps(dict(segments=segments), indent=1) + '\n')
    print('%s: %d segments; final mode %04x wave %d lives %d camera %d' % (
        a.out, len(segments), word(ram, 0xFF00), word(ram, 0xFF04), ram[0xFF20], word(ram, CAMERA)))
    if a.output:
        raw.close()
        (a.output / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
        (a.output / 'metadata.json').write_text(json.dumps(dict(
            backend='Genesis Plus GX libretro (tools/bot-play.py)', core=str(Path(a.core).resolve()),
            scenario_sha256=hashlib.sha256(a.out.read_bytes()).hexdigest(),
            rom_sha256=hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(), frames=g.frame, ram_first_frame=1,
            aids_until=a.aids_until, weaken=a.weaken, player_health=a.health), indent=2) + '\n')
    g.close()


if __name__ == '__main__':
    main()
