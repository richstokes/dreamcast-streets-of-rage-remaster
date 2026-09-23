#!/usr/bin/env python3
"""State-synchronised comparison of the native port with the original ROM.

Long replays drift apart once a gameplay update finishes on the other side of a
VBlank in one backend. To compare content far into a replay, this tool:

1. exports the original's machine state at the end of FRAME (Genesis Plus GX
   profiling core, genesis_reference.py --export-state): 68000 registers, work
   RAM, VDP, Z80 RAM/registers. FRAME must have no incremental Nemesis stream
   in flight; the export reports the first such frame and this tool stops if it
   is not FRAME. A state.bin already in OUTPUT_DIR (for example from
   bot-play.py --export-state) is used as it is;
2. runs the native port with the same scenario until it waits for VBlank in the
   same main-loop wait as the original did, replaces its state with the
   exported one (SOR_STATE_SYNC) and continues with the original's input from
   FRAME on;
3. compares the RAM of both frame by frame from there (`original` must be a
   raw-RAM reference run of the same scenario: genesis_reference.py --raw-ram
   or bot-play.py --output).

This is a comparison technique, not play: the native run does not reach FRAME
by itself. YM2612 and PSG state are not transferred (the game does not read
them).

  tools/state-sync.py ROM SCENARIO ORIGINAL_DIR FRAME OUTPUT_DIR [--frames N]
"""
import argparse, json, mmap, os, platform, re, subprocess, sys
from pathlib import Path

from genesis_reference import BUTTONS, segment_masks
from sor_ram import OBJECT_SLOTS, OBJECT_REGIONS, MODE, WAVE, CAMERA, P1_LIVES

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'build/gpgx-profile' / ('genesis_plus_gx_libretro.dylib' if platform.system() == 'Darwin' else 'genesis_plus_gx_libretro.so')
# Work RAM the port represents differently (host-owned), left out of the whole-RAM
# comparison: the incremental Nemesis queue's stream cursor and saved decoder
# registers, the Nemesis code table, and stack below the main loop's frame.
HOST_OWNED = [(0xDCD0, 0xDD2C), (0xF600, 0xF800), (0xFE00, 0xFEFC)]
MAILBOX = 0xFA00   # 0 unless the main loop is waiting for VBlank (an update or a load is running)


def masked(ram):
    out = bytearray(ram)
    for s, e in HOST_OWNED: out[s:e] = bytes(e - s)
    return bytes(out)


def run_inputs(scenario, events):
    """Input masks for each reference run index, following its gate events."""
    gates = {e['segment']: e['idle_frames'] for e in events}
    out = []
    for index, segment in enumerate(scenario['segments']):
        mask = tuple(segment_masks(segment))
        count = gates.get(index, 0) if 'wait' in segment else segment['frames']
        out += [mask] * count
    return out


def write_replay(inputs, path):
    names = {v: k for k, v in BUTTONS.items()}
    segments = []
    for mask in inputs:
        if segments and segments[-1][0] == mask and segments[-1][1] < 60000: segments[-1][1] += 1
        else: segments.append([mask, 1])
    if len(segments) > 4096: segments = segments[:4096]
    buttons = lambda m: [names[b] for b in sorted(names) if m >> b & 1]
    scenario = {'segments': [dict(frames=n, p1=buttons(m[0]), p2=buttons(m[1])) for m, n in segments]}
    json_path = path.with_suffix('.json'); json_path.write_text(json.dumps(scenario))
    subprocess.run([sys.executable, str(ROOT / 'tools/replay.py'), str(json_path), str(path)], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path); ap.add_argument('scenario', type=Path); ap.add_argument('original', type=Path)
    ap.add_argument('frame', type=int); ap.add_argument('output', type=Path)
    ap.add_argument('--frames', type=int, default=3000, help='frames to compare after the sync')
    ap.add_argument('--reuse', action='store_true', help='compare an existing native run again')
    ap.add_argument('--core', type=Path, default=CORE, help='profiling core (tools/build-profile-core.sh)')
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    rom = str(a.rom.resolve()); scenario = json.loads(a.scenario.read_text())
    state = a.output / 'state.bin'
    if not state.exists():   # the export replays the scenario in the reference; reuse it
        run = subprocess.run([sys.executable, str(ROOT / 'tools/genesis_reference.py'), str(a.core),
                              rom, str(a.scenario), str(a.output / 'reference-run'),
                              '--export-state', '%d:%s' % (a.frame, state.resolve())],
                             check=True, capture_output=True, text=True)
        used = re.search(r'state exported at frame (\d+)', run.stdout)
        if not used: raise SystemExit('the reference run did not export a state')
        if int(used.group(1)) != a.frame:
            raise SystemExit('an incremental decode is in flight at frame %d; state.bin is from frame %s, '
                             'run again with that frame' % (a.frame, used.group(1)))
    events = json.loads((a.original / 'events.json').read_text())
    inputs = run_inputs(scenario, events)[a.frame:a.frame + a.frames + 2]
    after = a.output / 'after.bin'; write_replay(inputs, after)
    before = a.output / 'before.bin'
    subprocess.run([sys.executable, str(ROOT / 'tools/replay.py'), str(a.scenario), str(before)], check=True)
    raw = a.output / 'ram.bin'
    if not (a.reuse and raw.exists()):
        env = dict(os.environ, SOR_STATE_SYNC='%s:%s' % (state.resolve(), after.resolve()), SOR_AUDIO='1')
        with (a.output / 'run.log').open('w') as log:
            subprocess.run([str(ROOT / 'build/headless/sor-headless'), rom, str(before), str(raw)],
                           stdout=log, stderr=subprocess.STDOUT, env=env, timeout=900)
    found = re.search(r'STATE_SYNC frame=(\d+)', (a.output / 'run.log').read_text())
    if not found: raise SystemExit('native run did not sync; see run.log')
    k = int(found.group(1))

    gfile, nfile = open(a.original / 'ram.bin', 'rb'), open(raw, 'rb')
    G = mmap.mmap(gfile.fileno(), 0, access=mmap.ACCESS_READ)
    N = mmap.mmap(nfile.fileno(), 0, access=mmap.ACCESS_READ)
    gram = lambda f: G[(f - 1) * 65536:f * 65536]          # original: frame 1 at offset 0
    nram = lambda i: N[i * 65536:(i + 1) * 65536]          # native raw capture index
    count = min(a.frames, len(N) // 65536 - k, len(G) // 65536 - a.frame + 1)
    first = {}; equal_ram = equal_game_ram = 0; unsettled = []
    for j in range(count):
        g, n = gram(a.frame + j), nram(k + j)
        if g == n: equal_ram += 1
        if masked(g) == masked(n): equal_game_ram += 1; continue
        if not g[MAILBOX] and not n[MAILBOX]:
            # Both captured while an update or a load runs: how far each got by
            # the VBlank is timing within it, not behaviour (the port's
            # decompressors also write their output at once and charge the
            # ROM routine's time afterwards). Reported separately; a difference
            # in behaviour persists into the next settled frame.
            unsettled.append(j); continue
        first.setdefault('game_ram', j)
        for slot in OBJECT_SLOTS:
            if not g[slot] and not n[slot]: continue
            for name, (s, e) in OBJECT_REGIONS.items():
                if name not in first and g[slot + s:slot + e] != n[slot + s:slot + e]: first[name] = j
        for name, (addr, width) in dict(mode=(MODE, 2), wave=(WAVE, 2), camera=(CAMERA, 2), p1_lives=(P1_LIVES, 1)).items():
            if name not in first and g[addr:addr + width] != n[addr:addr + width]: first[name] = j
    result = dict(scenario=str(a.scenario), sync_frame=a.frame, native_sync_frame=k, compared_frames=count,
                  identical_ram_frames=equal_ram, identical_game_ram_frames=equal_game_ram,
                  unsettled_differences=unsettled, first_difference=min(first.values()) if first else None,
                  first_difference_by_field=first)
    (a.output / 'result.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
