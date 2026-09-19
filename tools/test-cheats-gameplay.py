#!/usr/bin/env python3
"""Exercise the native cheats menu through controller replay and actual game code.

Run after tools/build-headless.sh, passing the locked ROM. Captures and ROM-derived
RAM stay under ignored build/cheats-tests. This is a native feature test, not a
claim of original-ROM parity for the later rounds.
"""
import argparse
import json
import mmap
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def tap(button):
    return [{'frames': 1, 'p1': [button]}, {'frames': 2}]


def boot_menu():
    return ([{'frames': 360}] + tap('START') + [{'frames': 120}] +
            tap('START') + [{'frames': 180}])


def configure(round_number, lives, enabled=False):
    segments = tap('CHEATS')
    for _ in range(round_number - 1): segments += tap('RIGHT')
    segments += tap('DOWN')
    for _ in range((lives - 3) % 9): segments += tap('RIGHT')
    if enabled:
        for _ in range(3): segments += tap('DOWN') + tap('RIGHT')
    return segments + tap('BACK')


def start(two_players=False):
    confirm = ([{'frames': 1, 'p1': ['START'], 'p2': ['START']}, {'frames': 2}]
               if two_players else tap('START'))
    return ((tap('DOWN') if two_players else []) + tap('START') + [{'frames': 180}] +
            confirm + [{'frames': 1800, 'wait': {'address': 0xff01, 'value': 0x16}},
                           {'frames': 180}])


def run(rom, name, segments):
    directory = ROOT / 'build/cheats-tests' / name
    directory.mkdir(parents=True, exist_ok=True)
    scenario, replay, trace = (directory / n for n in ('scenario.json', 'replay.bin', 'ram.bin'))
    scenario.write_text(json.dumps({'segments': segments}, indent=2) + '\n')
    subprocess.run([sys.executable, str(ROOT / 'tools/replay.py'), str(scenario), str(replay)], check=True)
    with (directory / 'run.log').open('w') as log:
        subprocess.run([str(ROOT / 'build/headless/sor-headless'), str(rom), str(replay), str(trace)],
                       env={**os.environ, 'SOR_AUDIO': '0'}, stdout=log, stderr=subprocess.STDOUT,
                       timeout=90, check=True)
    with trace.open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as memory:
            final = memory[-65536:]
            gameplay = [memory[i:i+65536] for i in range(0, len(memory), 65536)
                        if memory[i+0xff00:i+0xff02] == b'\x00\x16']
    (directory / 'final.ram').write_bytes(final)
    trace.unlink() # Retain the final snapshot/capture and compact assertions.
    return final, gameplay


def word(ram, address):
    return int.from_bytes(ram[address:address+2], 'big')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    args = parser.parse_args()
    rom = args.rom.resolve()
    for round_number in range(1, 9):
        lives = (round_number % 9) + 1
        ram, _ = run(rom, f'round-{round_number}', boot_menu() + configure(round_number, lives) + start())
        assert word(ram, 0xff00) == 0x16, (round_number, 'not playing')
        assert word(ram, 0xff02) == round_number - 1, (round_number, 'wrong round')
        assert ram[0xff20] == lives, (round_number, 'wrong lives', ram[0xff20])
        assert ram[0xb800] == 1 and word(ram, 0xb832) > 0, (round_number, 'player absent')
        print(f'Round {round_number}: starts with {lives} lives', flush=True)

    # Both players enter with one life; cheats top them up. Two police calls
    # and a long idle encounter exercise refill and lethal-hit protection.
    ram, frames = run(rom, 'two-player-cheats', boot_menu() + configure(1, 1, True) + start(True) +
                      tap('A') + [{'frames': 600}] + tap('A') + [{'frames': 1200}])
    assert ram[0xb800] == ram[0xb880] == 1
    assert ram[0xff20] == ram[0xff23] == 9
    assert ram[0xff21] == ram[0xff24] == 1
    assert word(ram, 0x6020) == word(ram, 0x6054) == 0x6d2 # Both life counters show nine.
    assert word(ram, 0x602a) == word(ram, 0x603a) == 0x6c2 # Both police counters show one.
    assert word(ram, 0xb832) == word(ram, 0xb8b2) == 80
    assert all(word(r, a) == 80 for r in frames for a in (0xb832, 0xb8b2) if word(r, a) != 0)
    print('Two players: infinite lives/health, repeated police calls, one-life start pass', flush=True)

    ram, _ = run(rom, 'round-8-cheats', boot_menu() + configure(8, 3, True) + start() + tap('A') + [{'frames': 120}])
    assert word(ram, 0xff02) == 7 and ram[0xff21] == 0
    print('Round 8: police remains disabled', flush=True)

    ram, _ = run(rom, 'gameplay-menu-resume', boot_menu() + start() + tap('CHEATS') +
                 [{'frames': 60}] + tap('START') + [{'frames': 120}])
    assert word(ram, 0xff00) == 0x16 and word(ram, 0xff06) == 0
    print('In-game menu: Start closes without leaking a pause press', flush=True)

    # End a replay inside the menu to produce a visual capture, without advancing
    # game time while it is open. The native host explicitly supports this exit.
    run(rom, 'menu', boot_menu() + tap('CHEATS') + [{'frames': 10}])
    print('Cheats menu capture written under build/cheats-tests/menu', flush=True)


if __name__ == '__main__':
    main()
