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

PLAYER, PLAYER2, HEALTH, CAMERA, LIVES = 0xB800, 0xB880, 0xB832, 0xE002, 0xFF20
LIVES2 = 0xFF23
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


ITEMS = (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x47, 0x4B)       # weapons $08-$0C, food $47/$4B


def prompt(ram, frame, me, answer):
    """Buttons while the player is out (type $0F): name entry, then the continue prompt.

    With object+$4B bit 7 set the object runs the high-score name entry ($56E6):
    Start confirms each letter as it is offered. Then the Yes/No prompt
    ($52AE): up/down toggle +$63 (0 = yes), a face button or Start confirms.
    Buttons are pulsed, since the game acts on newly pressed ones, with an odd
    period: the press flag lasts one VBlank and the game updates every second
    one, so pulses of one parity can all go unseen."""
    if frame % 15 >= 2: return []
    if ram[me + 0x4B] & 0x80: return ['START']
    if ram[me + 0x30] == 4 and ram[me + 0x63] != (answer == 'no'): return ['DOWN']
    return ['B']


def toward(dx, dy, reach=34, lane=6):
    buttons = []
    if abs(dy) > lane: buttons.append('DOWN' if dy > 0 else 'UP')
    if abs(dx) > reach: buttons.append('RIGHT' if dx > 0 else 'LEFT')
    return buttons


class Bot:
    """Closed-loop policy for one player (object slot `me`).

    By default: close on the nearest live enemy, then punch. Options add throws
    from grabs, walking to weapons and food, and (`rival`) attacking the other
    player when told to spar, for friendly fire.
    """

    def __init__(self, me, answer='yes', throws=False, pickups=False, rival=None):
        self.me, self.answer, self.throws, self.pickups, self.rival = me, answer, throws, pickups, rival
        self.grab_from = None; self.grabs = 0; self.chasing = None; self.given_up = set()

    def buttons(self, ram, frame, lure=False, spar=False):
        """The camera stops while a wave is unfinished, so an enemy can stand
        beyond the edge the player is held at. `lure` backs away from it, which
        brings the enemy's own approach into range."""
        me = self.me
        if ram[me] == 0x0F: return prompt(ram, frame, me, self.answer)
        px, py = word(ram, me + 16), word(ram, me + 20)
        if self.throws and word(ram, me + 76):
            return self.throw(ram, frame)
        self.grab_from = None
        if spar and self.rival and ram[self.rival] == 0x01:
            dx, dy = word(ram, self.rival + 16) - px, word(ram, self.rival + 20) - py
            return toward(dx, dy) or (['B'] if frame % 6 < 2 else [])
        enemies = [(abs(word(ram, s + 16) - px) + abs(word(ram, s + 20) - py) * 2, s)
                   for s in SLOTS if live(ram, s)]
        if self.pickups:
            item = self.item(ram, frame, px, py, enemies)
            if item is not None: return item
        if not enemies: return ['RIGHT']                   # nothing awake: advance the camera
        _, slot = min(enemies)
        if lure: return ['LEFT'] if word(ram, slot + 16) > px else ['RIGHT']
        dx, dy = word(ram, slot + 16) - px, word(ram, slot + 20) - py
        buttons = toward(dx, dy)
        if buttons: return buttons
        return ['B'] if frame % 6 < 2 else []              # punch, with gaps for the combo

    def throw(self, ram, frame):
        """From a front grab: alternately away + attack (state $63) and a
        vault (jump) followed by attack from behind ($69/$71)."""
        if self.grab_from is None: self.grab_from = frame; self.grabs += 1
        t = frame - self.grab_from
        away = 'RIGHT' if ram[self.me + 0x30] & 1 else 'LEFT'   # bit 0 of the action: facing left
        if self.grabs % 2:
            return [away, 'B'] if t % 15 < 2 else [away]
        if t < 2: return ['C']
        return ['B'] if t >= 16 and t % 15 < 2 else []

    def item(self, ram, frame, px, py, enemies):
        """Walk to a weapon or food on the ground and press attack over it,
        while no enemy is near. An item not collected after 300 frames of
        walking to it is dropped from consideration."""
        if any(abs(word(ram, s + 16) - px) < 100 for _, s in enemies): return None
        held = {word(ram, p + 0x5E) for p in (PLAYER, PLAYER2) if ram[p + 0x60]}
        camera = word(ram, CAMERA)
        items = [(abs(word(ram, s + 16) - px) + abs(word(ram, s + 20) - py), s) for s in SLOTS
                 if ram[s] in ITEMS and s not in held and s not in self.given_up
                 and camera <= word(ram, s + 16) <= camera + 320
                 and not (ram[s] < 0x40 and ram[self.me + 0x60])]
        if not items: self.chasing = None; return None
        _, slot = min(items)
        if not self.chasing or self.chasing[0] != slot: self.chasing = [slot, 0]
        self.chasing[1] += 1
        if self.chasing[1] > 300: self.given_up.add(slot); self.chasing = None; return None
        buttons = toward(word(ram, slot + 16) - px, word(ram, slot + 20) - py, reach=8, lane=4)
        return buttons or (['B'] if frame % 15 < 2 else [])


def policy(ram, frame, lure=False, answer='yes'):
    """The default single-player policy (Bot with no options)."""
    return Bot(PLAYER, answer).buttons(ram, frame, lure)


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
    ap.add_argument('--continue', dest='answer', choices=('yes', 'no'), default='yes',
                    help='answer to the continue prompt once the player is out')
    ap.add_argument('--throws', action='store_true', help='throw grabbed enemies instead of kneeing them')
    ap.add_argument('--pickups', action='store_true', help='collect weapons and food when no enemy is near')
    ap.add_argument('--players', type=int, choices=(1, 2), default=1,
                    help='2: drive player 2 with the same policy (the prologue must start a two-player game)')
    ap.add_argument('--join', type=int, metavar='FRAME', help='one-player game: player 2 presses Start from FRAME '
                    'until the game lets them in, then plays with the same policy')
    ap.add_argument('--aid-players', type=int, choices=(1, 2), default=2,
                    help='1: keep only player 1 alive (player 2 can run out of lives and continue)')
    ap.add_argument('--spar', metavar='PERIOD:LENGTH', help='two players: for LENGTH frames of every PERIOD, '
                    'player 2 attacks player 1 (friendly fire)')
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
    options = dict(answer=a.answer, throws=a.throws, pickups=a.pickups)
    bots = [Bot(PLAYER, **options)] + ([Bot(PLAYER2, rival=PLAYER, **options)] if a.players == 2 or a.join else [])
    spar = tuple(int(v) for v in a.spar.split(':')) if a.spar else None
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
            if a.players == 2 and a.aid_players == 2:
                if 0 < word(ram, PLAYER2 + 50) < a.health: g.poke(PLAYER2 + 50, a.health, 2)
                if ram[LIVES2] < 3: g.poke(LIVES2, 3, 1)
            if a.weaken:
                for slot in SLOTS:
                    if live(ram, slot) and ram[slot] <= 0x27 and word(ram, slot + 50) > 1:
                        g.poke(slot + 50, 1, 2)
        lure = held > a.stuck and held % (a.stuck * 2) < a.stuck // 2
        sparring = bool(spar) and frame % spar[0] < spar[1]
        pressed = [bot.buttons(ram, frame, lure, spar=sparring) for bot in bots] + [[]]
        if a.join and ram[PLAYER2] != 0x01:
            # Not in play yet: idle, then pulse Start (odd period, see prompt()).
            pressed[1] = ['START'] if g.frame >= a.join and frame % 15 < 2 else []
        if segments and 'wait' not in segments[-1] and [segments[-1].get(k, []) for k in ('p1', 'p2')] == pressed[:2]:
            segments[-1]['frames'] += 1
        else: segments.append(dict(frames=1, **{k: b for k, b in zip(('p1', 'p2'), pressed) if b}))
        ram = g.step(*(sum(1 << BUTTONS[b] for b in buttons) for buttons in pressed[:2]))
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
            aids_until=a.aids_until, weaken=a.weaken, player_health=a.health, players=a.players, throws=a.throws,
            pickups=a.pickups, spar=a.spar, answer=a.answer, aid_players=a.aid_players, join=a.join), indent=2) + '\n')
    g.close()


if __name__ == '__main__':
    main()
