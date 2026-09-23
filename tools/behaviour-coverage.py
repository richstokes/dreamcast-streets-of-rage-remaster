#!/usr/bin/env python3
"""Behaviour the original exercises in each matched comparison window.

Reads the original's raw RAM captures (genesis_reference.py --raw-ram or
bot-play.py --output) and counts, from the gate over the given number of
frames: object types present, enemy hits and knockouts, player grabs and throws
(front $62, from behind $70), weapons picked up and let go, food eaten (health
gained), police specials used, player health drops, hits between the two
players with no enemy near, continue prompts (player type $0F), continues
taken, game overs (mode $0C) and player 2 joining a game in progress.

The windows come from a JSON list: for a replay compared from its gate,
{"name", "run", "gate", "frames"}; for a state-synchronised window
(tools/state-sync.py), {"name", "run", "sync"} with the sync's result.json,
counted from its sync frame over the frames that matched.

  tools/behaviour-coverage.py WINDOWS.json
"""
import argparse
import collections
import json
import mmap
from pathlib import Path

from sor_ram import PLAYER1, PLAYER2, MODE, WAVE, P1_LIVES, OBJECT_SLOTS, word, is_enemy

PLAYERS = (PLAYER1, PLAYER2)
OTHERS = OBJECT_SLOTS[2:]
POLICE_STOCK = 0xFF21
MODE_GAME_OVER = 0x0C
TYPE_OUT = 0x0F          # a player who has run out of lives: name entry, then the continue prompt
THROWS = {0x62: 'front', 0x70: 'behind'}


def ram_frames(path):
    f = open(path / 'ram.bin', 'rb')
    m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    return lambda frame: m[(frame - 1) * 65536:frame * 65536]      # frame 1 is the first capture


def analyse(path, gate, length):
    R = ram_frames(path)
    types, hits, deaths, modes, throws, pickups = (collections.Counter() for _ in range(6))
    waves = set()
    weapons = set()
    counts = collections.Counter()
    prev = None
    for frame in range(gate, gate + length):
        r = R(frame)
        modes['%02x' % word(r, MODE)] += 1
        waves.add(word(r, WAVE))
        for s in OTHERS:
            if r[s]:
                types[r[s]] += 1
        for p in PLAYERS:
            if word(r, p + 76):
                counts['grab_frames'] += 1
            if r[p + 0x60]:
                counts['weapon_frames'] += 1
                weapons.add(r[p + 0x60])
        if prev is not None:
            for s in OTHERS:
                t = r[s]
                if t and prev[s] == t and is_enemy(t):
                    h0, h1 = word(prev, s + 50), word(r, s + 50)
                    if h1 < h0:
                        hits[t] += 1
                    if h0 and not h1:
                        deaths[t] += 1
            if r[POLICE_STOCK] < prev[POLICE_STOCK]:
                counts['police_specials_used'] += 1
            if word(r, MODE) == MODE_GAME_OVER and word(prev, MODE) != MODE_GAME_OVER:
                counts['game_overs'] += 1
            if r[PLAYER2] == 0x01 and prev[PLAYER2] == 0x00:
                counts['joins'] += 1
            if r[P1_LIVES] < prev[P1_LIVES]:
                counts['lives_lost'] += 1
            enemies = [s for s in OTHERS if is_enemy(r[s])]
            for p in PLAYERS:
                if r[p] and word(r, p + 50) < word(prev, p + 50):
                    counts['player_health_drops'] += 1
                if word(r, p + 76) and not word(prev, p + 76):
                    counts['grab_starts'] += 1
                if r[p] == TYPE_OUT and prev[p] != TYPE_OUT:
                    counts['continue_prompts'] += 1
                if r[p] == 0x01 and prev[p] == TYPE_OUT:
                    counts['continues'] += 1
                if r[p] != 0x01 or prev[p] != 0x01:
                    continue
                act = r[p + 0x30] & 0xFE
                if act != prev[p + 0x30] & 0xFE and act in THROWS:
                    throws[THROWS[act]] += 1
                if r[p + 0x60] and not prev[p + 0x60]:
                    pickups[hex(r[p + 0x60])] += 1
                if prev[p + 0x60] and not r[p + 0x60]:
                    counts['weapons_let_go'] += 1
                if 0 < word(prev, p + 50) < word(r, p + 50):
                    counts['food_eaten'] += 1
                # A hit taken beside the other player with no enemy within reach.
                other = PLAYER2 if p == PLAYER1 else PLAYER1
                distance = lambda a: abs(word(r, p + 16) - word(r, a + 16)) + abs(word(r, p + 20) - word(r, a + 20))
                if (r[other] == 0x01 and word(r, p + 50) < word(prev, p + 50)
                        and abs(word(r, p + 16) - word(r, other + 16)) < 50 and abs(word(r, p + 20) - word(r, other + 20)) < 12
                        and all(distance(e) > 60 for e in enemies)):
                    counts['friendly_hits'] += 1
        prev = r
    keys = ('grab_starts', 'grab_frames', 'weapon_frames', 'police_specials_used', 'lives_lost', 'weapons_let_go',
            'food_eaten', 'friendly_hits', 'continue_prompts', 'continues', 'game_overs', 'joins', 'player_health_drops')
    return dict(frames=length, modes=dict(modes), waves=sorted(waves),
                object_types={hex(k): v for k, v in sorted(types.items())},
                weapons=sorted(hex(x) for x in weapons), throws=dict(throws), weapon_pickups=dict(pickups),
                enemy_hits={hex(k): v for k, v in hits.items()}, enemy_knockouts={hex(k): v for k, v in deaths.items()},
                **{k: counts[k] for k in keys})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('windows', type=Path, help='JSON list of windows; run directories are relative to the repository root')
    a = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = {}
    for w in json.loads(a.windows.read_text()):
        run = root / w['run']
        if 'sync' in w:
            r = json.loads((root / w['sync'] / 'result.json').read_text())
            gate = r['sync_frame']
            length = r['compared_frames'] if r['first_difference'] is None else r['first_difference']
        else:
            gate, length = w['gate'], w['frames']
        out[w['name']] = analyse(run, gate, length)
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
