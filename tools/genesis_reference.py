#!/usr/bin/env python3
"""Run the original ROM in a headless Genesis Plus GX libretro core as the reference.

A scenario (reference/scenarios/*.json) is a list of input segments, each held
for a number of frames, optionally with a state gate: the segment ends as soon
as a byte of work RAM takes a value, so later input starts at the same point of
the game regardless of timing. Each frame's observation goes to trace.jsonl;
--raw-ram keeps every frame's work RAM; the profiling core built by
tools/build-profile-core.sh adds per-PC cycle profiles, call timelines, YM2612
write logs and machine-state export.
"""
import argparse
import ctypes as C
from contextlib import nullcontext
import hashlib
import json
from pathlib import Path
import struct
import wave

from sor_ram import OBJECT_SLOTS, MODE, STAGE, WAVE, CAMERA, P1_LIVES, P2_LIVES, word

# libretro's physical button IDs.
BUTTONS = {'B': 0, 'A': 1, 'C': 8, 'START': 3, 'UP': 4, 'DOWN': 5, 'LEFT': 6, 'RIGHT': 7}


class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p), ('size', C.c_size_t), ('meta', C.c_char_p)]


class Geometry(C.Structure):
    _fields_ = [('base_width', C.c_uint), ('base_height', C.c_uint), ('max_width', C.c_uint), ('max_height', C.c_uint),
                ('aspect_ratio', C.c_float)]


class Timing(C.Structure):
    _fields_ = [('fps', C.c_double), ('sample_rate', C.c_double)]


class AVInfo(C.Structure):
    _fields_ = [('geometry', Geometry), ('timing', Timing)]


class Variable(C.Structure):
    _fields_ = [('key', C.c_char_p), ('value', C.c_char_p)]


ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
BATCH = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class Genesis:
    def __init__(self, core, rom):
        self.lib = C.CDLL(str(Path(core).resolve()))
        self.buttons = [0, 0]
        self.frame = 0
        self.audio_file = None
        self.pixel_format = 0
        self.image = None
        self.sample_frames = 0
        self.directory = str(Path(rom).resolve().parent).encode()
        self.variables = {b'genesis_plus_gx_region_detect': b'ntsc-u', b'genesis_plus_gx_system_hw': b'genesis',
                          b'genesis_plus_gx_bios': b'disabled'}
        self.callbacks = [ENV(self.environment), VIDEO(self.video), AUDIO(lambda l, r: None), BATCH(self.audio),
                          POLL(lambda: None), INPUT(self.input)]
        for name, cb in zip(['environment', 'video_refresh', 'audio_sample', 'audio_sample_batch', 'input_poll',
                             'input_state'], self.callbacks):
            getattr(self.lib, 'retro_set_' + name)(cb)
        self.lib.retro_get_memory_data.argtypes = [C.c_uint]
        self.lib.retro_get_memory_data.restype = C.c_void_p
        self.lib.retro_get_memory_size.argtypes = [C.c_uint]
        self.lib.retro_get_memory_size.restype = C.c_size_t
        self.lib.retro_load_game.argtypes = [C.POINTER(GameInfo)]
        self.lib.retro_load_game.restype = C.c_bool
        self.lib.retro_init()
        self.rom = C.create_string_buffer(Path(rom).read_bytes())
        info = GameInfo(str(Path(rom).resolve()).encode(), C.cast(self.rom, C.c_void_p), len(self.rom) - 1, None)
        if not self.lib.retro_load_game(C.byref(info)):
            raise RuntimeError('libretro load failed')
        av = AVInfo()
        self.lib.retro_get_system_av_info(C.byref(av))
        self.sample_rate = av.timing.sample_rate
        self.lib.retro_set_controller_port_device(0, 1)
        self.lib.retro_set_controller_port_device(1, 1)

    def environment(self, cmd, data):
        cmd &= 0xffff
        if cmd in (9, 30, 31):      # system, save and core-assets directories
            C.cast(data, C.POINTER(C.c_char_p))[0] = self.directory
            return True
        if cmd == 10:               # pixel format
            self.pixel_format = C.cast(data, C.POINTER(C.c_int))[0]
            return self.pixel_format in (0, 1, 2)
        if cmd == 15:               # core variable
            v = C.cast(data, C.POINTER(Variable)).contents
            v.value = self.variables.get(v.key)
            return v.value is not None
        if cmd in (3, 17):          # can dupe, variable update
            C.cast(data, C.POINTER(C.c_bool))[0] = False
            return True
        return cmd in (11, 16, 18, 35)

    def input(self, port, device, index, button):
        return int(port < 2 and bool(self.buttons[port] & (1 << button)))

    def audio(self, data, n):
        self.sample_frames += n
        if self.audio_file:
            self.audio_file.writeframesraw(C.string_at(data, n * 4))
        return n

    def capture_audio(self, path):
        self.audio_file = wave.open(str(path), 'wb')
        self.audio_file.setnchannels(2)
        self.audio_file.setsampwidth(2)
        self.audio_file.setframerate(round(self.sample_rate))

    def video(self, data, w, h, pitch):
        if data:
            self.image = (C.string_at(data, pitch * h), w, h, pitch, self.pixel_format)

    def ram(self):
        """Work RAM in Mega Drive byte order (the core stores each byte at address^1)."""
        n = self.lib.retro_get_memory_size(2)
        p = self.lib.retro_get_memory_data(2)
        b = bytearray(C.string_at(p, n))
        b[0::2], b[1::2] = b[1::2], b[0::2]
        return bytes(b)

    def poke(self, addr, value, width):
        """Reference-only RAM writes, to reach content a replay cannot survive to (state-sync.py)."""
        p = self.lib.retro_get_memory_data(2)
        for i, b in enumerate(value.to_bytes(width, 'big')):
            C.c_ubyte.from_address(p + ((addr + i) ^ 1)).value = b

    def step(self, p1=0, p2=0):
        self.buttons = [p1, p2]
        self.lib.retro_run()
        self.frame += 1
        return self.ram()

    def capture(self, path):
        from PIL import Image
        raw, w, h, pitch, fmt = self.image
        out = bytearray()
        for y in range(h):
            for x in range(w):
                if fmt == 1:                                        # XRGB8888
                    v = struct.unpack_from('<I', raw, y * pitch + x * 4)[0]
                    rgb = (v >> 16 & 255, v >> 8 & 255, v & 255)
                else:                                               # RGB565 (2) or 0RGB1555 (0)
                    v = struct.unpack_from('<H', raw, y * pitch + x * 2)[0]
                    green_bits = 63 if fmt == 2 else 31
                    rgb = ((v >> (11 if fmt == 2 else 10) & 31) * 255 // 31, (v >> 5 & green_bits) * 255 // green_bits,
                           (v & 31) * 255 // 31)
                out.extend(rgb)
        Image.frombytes('RGB', (w, h), bytes(out)).save(path)

    def close(self):
        if self.audio_file:
            self.audio_file.close()
        self.lib.retro_unload_game()
        self.lib.retro_deinit()


def decoder_idle(ram):
    """Whether the machine state can be handed to the native port.

    The port keeps the incremental Nemesis decoder's cursor in host state, which
    the machine state does not carry, so no stream may be in flight: queue head
    $FFDCD0 and tiles remaining $FFDD28 are both zero between streams."""
    return not any(ram[0xDCD0:0xDCD4]) and not any(ram[0xDD28:0xDD2A])


def segment_masks(segment):
    """(player 1, player 2) button masks held through a scenario segment."""
    return [sum(1 << BUTTONS[b] for b in segment.get(k, [])) for k in ('p1', 'p2')]


def gate_open(gate, ram):
    return ram[gate['address']] & gate.get('mask', 255) == gate['value']


def observation(ram, frame):
    actors = [dict(slot=a, type=ram[a], x=word(ram, a + 16), y=word(ram, a + 20), z=word(ram, a + 24),
                   state=word(ram, a + 48), health=word(ram, a + 50)) for a in OBJECT_SLOTS if ram[a]]
    return dict(frame=frame, mode=word(ram, MODE), stage=word(ram, STAGE), wave=word(ram, WAVE), camera=word(ram, CAMERA),
                p1_lives=ram[P1_LIVES], p2_lives=ram[P2_LIVES], actors=actors, ram_sha256=hashlib.sha256(ram).hexdigest())


def parse_range_spec(spec, parts):
    """FIRST:LAST:PATH style options: the leading integers and the trailing path."""
    fields = spec.split(':', parts)
    return [int(f) for f in fields[:parts]] + fields[parts:]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('core', help='Genesis Plus GX libretro core')
    p.add_argument('rom')
    p.add_argument('scenario')
    p.add_argument('output', type=Path)
    p.add_argument('--raw-ram', action='store_true', help='keep every frame\'s work RAM in ram.bin')
    p.add_argument('--audio-wav', action='store_true', help='keep the sound output in audio.wav')
    p.add_argument('--profile', action='append', default=[], metavar='FIRST:LAST:PATH',
                   help='per-PC 68000 cycles for frames FIRST..LAST (profiling core; tools/build-profile-core.sh)')
    p.add_argument('--watch', metavar='PCS:LAST:PATH',
                   help='profiling core: time of each entry to the hex addresses in file PCS until frame LAST')
    p.add_argument('--ym-log', metavar='FIRST:LAST:PATH',
                   help='profiling core: every YM2612 write in frames FIRST..LAST (frame, clock, port, value)')
    p.add_argument('--poke', action='append', default=[], metavar='ADDR:VALUE[:WIDTH[:FIRST:LAST]]',
                   help='write VALUE into work RAM at ADDR (hex) before each frame, optionally only in frames '
                        'FIRST..LAST; a reference-only aid for reaching late content (tools/state-sync.py)')
    p.add_argument('--export-state', metavar='FRAME:PATH',
                   help='profiling core: machine state at the end of the first frame from FRAME on that the native '
                        'port can take (no incremental decode in flight); the frame used is reported')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    g = Genesis(a.core, a.rom)
    if a.audio_wav:
        g.capture_audio(a.output / 'audio.wav')
    scenario = json.loads(Path(a.scenario).read_text())

    profiles = [parse_range_spec(v, 2) for v in a.profile]
    if profiles:
        g.lib.sor_profile_stop.argtypes = [C.c_char_p]
    pokes = []
    for spec in a.poke:
        f = spec.split(':')
        pokes.append((int(f[0], 16), int(f[1], 0), int(f[2]) if len(f) > 2 else 1,
                      int(f[3]) if len(f) > 3 else 1, int(f[4]) if len(f) > 4 else None))
    watch = None
    if a.watch:
        pcs_file, last, path = a.watch.split(':', 2)
        pcs = [int(x, 16) for x in Path(pcs_file).read_text().split()]
        g.lib.sor_watch_stop.argtypes = [C.c_char_p]
        g.lib.sor_watch_start((C.c_uint * len(pcs))(*pcs), len(pcs))
        watch = (int(last), path)
    ym_log = parse_range_spec(a.ym_log, 2) if a.ym_log else None
    if ym_log:
        g.lib.sor_ym_stop.argtypes = [C.c_char_p]
    export = parse_range_spec(a.export_state, 1) if a.export_state else None
    if export:
        g.lib.sor_export_state.argtypes = [C.c_char_p]

    ram = g.ram()
    events = []
    raw_context = (a.output / 'ram.bin').open('wb') if a.raw_ram else nullcontext()
    with (a.output / 'trace.jsonl').open('w') as trace, raw_context as raw:
        for index, segment in enumerate(scenario['segments']):
            masks = segment_masks(segment)
            gate = segment.get('wait')
            for elapsed in range(segment['frames'] + int(bool(gate))):
                if gate and gate_open(gate, ram):
                    events.append(dict(segment=index, frame=g.frame, idle_frames=elapsed))
                    break
                if gate and elapsed == segment['frames']:
                    raise RuntimeError(f'State gate {index} timed out')
                for first, _, _ in profiles:
                    if g.frame + 1 == first:
                        g.lib.sor_profile_start()
                if watch:
                    g.lib.sor_watch_frame(g.frame)
                if ym_log:
                    first, last, path = ym_log
                    if g.frame == first:
                        g.lib.sor_ym_start()
                    g.lib.sor_ym_frame(g.frame)
                    if g.frame == last + 1 and g.lib.sor_ym_stop(path.encode()):
                        raise RuntimeError('ym log write failed')
                for addr, value, width, first, last in pokes:
                    if first <= g.frame + 1 and (last is None or g.frame + 1 <= last):
                        g.poke(addr, value, width)
                ram = g.step(*masks)
                trace.write(json.dumps(observation(ram, g.frame)) + '\n')
                if export and g.frame >= export[0] and decoder_idle(ram):
                    if g.lib.sor_export_state(export[1].encode()):
                        raise RuntimeError('state export failed')
                    print('state exported at frame %d' % g.frame)
                    export = None
                if watch and g.frame == watch[0]:
                    if g.lib.sor_watch_stop(str(watch[1]).encode()):
                        raise RuntimeError('watch write failed')
                    watch = None
                for _, last, path in profiles:
                    if g.frame == last and g.lib.sor_profile_stop(str(path).encode()):
                        raise RuntimeError('profile write failed')
                if raw:
                    raw.write(ram)
            if 'capture' in segment:
                g.capture(a.output / (segment['capture'] + '.png'))
    (a.output / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
    metadata = dict(backend='Genesis Plus GX libretro', core=str(Path(a.core).resolve()),
                    core_sha256=hashlib.sha256(Path(a.core).read_bytes()).hexdigest(),
                    scenario_sha256=hashlib.sha256(Path(a.scenario).read_bytes()).hexdigest(),
                    rom_sha256=hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),
                    frames=g.frame, ram_first_frame=1, audio_sample_frames=g.sample_frames, audio_sample_rate=g.sample_rate)
    (a.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    g.close()


if __name__ == '__main__':
    main()
