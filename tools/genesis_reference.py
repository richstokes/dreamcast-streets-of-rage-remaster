#!/usr/bin/env python3
"""Headless Genesis Plus GX libretro reference. Never used by the Dreamcast game."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import wave

class GameInfo(C.Structure):
    _fields_=[('path',C.c_char_p),('data',C.c_void_p),('size',C.c_size_t),('meta',C.c_char_p)]
class Geometry(C.Structure):
    _fields_=[('base_width',C.c_uint),('base_height',C.c_uint),('max_width',C.c_uint),('max_height',C.c_uint),('aspect_ratio',C.c_float)]
class Timing(C.Structure):
    _fields_=[('fps',C.c_double),('sample_rate',C.c_double)]
class AVInfo(C.Structure):
    _fields_=[('geometry',Geometry),('timing',Timing)]
class Variable(C.Structure):
    _fields_=[('key',C.c_char_p),('value',C.c_char_p)]
ENV=C.CFUNCTYPE(C.c_bool,C.c_uint,C.c_void_p)
VIDEO=C.CFUNCTYPE(None,C.c_void_p,C.c_uint,C.c_uint,C.c_size_t)
AUDIO=C.CFUNCTYPE(None,C.c_int16,C.c_int16)
BATCH=C.CFUNCTYPE(C.c_size_t,C.c_void_p,C.c_size_t)
POLL=C.CFUNCTYPE(None)
INPUT=C.CFUNCTYPE(C.c_int16,C.c_uint,C.c_uint,C.c_uint,C.c_uint)

class Genesis:
    def __init__(self,core,rom):
        self.lib=C.CDLL(str(Path(core).resolve())); self.buttons=[0,0]; self.frame=0
        self.audio_file=None; self.pixel_format=0; self.image=None; self.sample_frames=0
        self.directory=str(Path(rom).resolve().parent).encode()
        self.variables={b'genesis_plus_gx_region_detect':b'ntsc-u',b'genesis_plus_gx_system_hw':b'genesis',b'genesis_plus_gx_bios':b'disabled'}
        self.callbacks=[ENV(self.environment),VIDEO(self.video),AUDIO(lambda l,r:None),BATCH(self.audio),POLL(lambda:None),INPUT(self.input)]
        for name,cb in zip(['environment','video_refresh','audio_sample','audio_sample_batch','input_poll','input_state'],self.callbacks):
            getattr(self.lib,'retro_set_'+name)(cb)
        self.lib.retro_get_memory_data.argtypes=[C.c_uint]; self.lib.retro_get_memory_data.restype=C.c_void_p
        self.lib.retro_get_memory_size.argtypes=[C.c_uint]; self.lib.retro_get_memory_size.restype=C.c_size_t
        self.lib.retro_load_game.argtypes=[C.POINTER(GameInfo)]; self.lib.retro_load_game.restype=C.c_bool
        self.lib.retro_init()
        self.rom=C.create_string_buffer(Path(rom).read_bytes())
        info=GameInfo(str(Path(rom).resolve()).encode(),C.cast(self.rom,C.c_void_p),len(self.rom)-1,None)
        if not self.lib.retro_load_game(C.byref(info)): raise RuntimeError('libretro load failed')
        av=AVInfo();self.lib.retro_get_system_av_info(C.byref(av));self.sample_rate=av.timing.sample_rate
        self.lib.retro_set_controller_port_device(0,1); self.lib.retro_set_controller_port_device(1,1)
    def environment(self,cmd,data):
        cmd &= 0xffff
        if cmd in (9,30,31): C.cast(data,C.POINTER(C.c_char_p))[0]=self.directory; return True
        if cmd==10: self.pixel_format=C.cast(data,C.POINTER(C.c_int))[0]; return self.pixel_format in (0,1,2)
        if cmd==15:
            v=C.cast(data,C.POINTER(Variable)).contents; v.value=self.variables.get(v.key); return v.value is not None
        if cmd==17: C.cast(data,C.POINTER(C.c_bool))[0]=False; return True
        if cmd in (16,18,11,35): return True
        if cmd==3: C.cast(data,C.POINTER(C.c_bool))[0]=False; return True
        return False
    def input(self,port,device,index,button):
        return int(port<2 and bool(self.buttons[port] & (1<<button)))
    def audio(self,data,n):
        self.sample_frames+=n
        if self.audio_file:self.audio_file.writeframesraw(C.string_at(data,n*4))
        return n
    def capture_audio(self,path):
        self.audio_file=wave.open(str(path),'wb');self.audio_file.setnchannels(2)
        self.audio_file.setsampwidth(2);self.audio_file.setframerate(round(self.sample_rate))
    def video(self,data,w,h,pitch):
        if data: self.image=(C.string_at(data,pitch*h),w,h,pitch,self.pixel_format)
    def ram(self):
        n=self.lib.retro_get_memory_size(2); p=self.lib.retro_get_memory_data(2)
        b=bytearray(C.string_at(p,n))
        # GPGX work_ram uses little-endian word storage on this little-endian core.
        b[0::2],b[1::2]=b[1::2],b[0::2]
        return bytes(b)
    def step(self,p1=0,p2=0):
        self.buttons=[p1,p2]; self.lib.retro_run(); self.frame+=1; return self.ram()
    def capture(self,path):
        from PIL import Image
        raw,w,h,pitch,fmt=self.image; out=bytearray()
        for y in range(h):
            for x in range(w):
                if fmt==1:
                    v=struct.unpack_from('<I',raw,y*pitch+x*4)[0]; rgb=(v>>16&255,v>>8&255,v&255)
                else:
                    v=struct.unpack_from('<H',raw,y*pitch+x*2)[0]
                    rgb=((v>>(11 if fmt==2 else 10)&31)*255//31,(v>>5&(63 if fmt==2 else 31))*255//(63 if fmt==2 else 31),(v&31)*255//31)
                out.extend(rgb)
        Image.frombytes('RGB',(w,h),bytes(out)).save(path)
    def close(self):
        if self.audio_file:self.audio_file.close()
        self.lib.retro_unload_game();self.lib.retro_deinit()

# Physical libretro IDs, independent of upstream's Genesis button masks.
BUTTONS={'B':0,'A':1,'C':8,'START':3,'UP':4,'DOWN':5,'LEFT':6,'RIGHT':7}
def observation(ram,frame):
    word=lambda a:int.from_bytes(ram[a:a+2],'big')
    actors=[]
    for a in (0xb800,0xb880, *range(0xb900,0xda00,128)):
        if ram[a]:actors.append(dict(slot=a,type=ram[a],x=word(a+16),y=word(a+20),z=word(a+24),state=word(a+48),health=word(a+50)))
    return dict(frame=frame,mode=word(0xff00),stage=word(0xff02),wave=word(0xff04),camera=word(0xe002),
                p1_lives=ram[0xff20],p2_lives=ram[0xff23],actors=actors,ram_sha256=hashlib.sha256(ram).hexdigest())

def main():
    p=argparse.ArgumentParser();p.add_argument('core');p.add_argument('rom');p.add_argument('scenario');p.add_argument('output',type=Path);p.add_argument('--raw-ram',action='store_true');p.add_argument('--audio-wav',action='store_true');p.add_argument('--profile',action='append',default=[],metavar='FIRST:LAST:PATH',help='per-PC 68000 cycles for frames FIRST..LAST (profiling core built with HOOK_CPU; see tools/build-profile-core.sh)');a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);g=Genesis(a.core,a.rom)
    if a.audio_wav:g.capture_audio(a.output/'audio.wav')
    scenario=json.loads(Path(a.scenario).read_text())
    from contextlib import nullcontext
    ram=g.ram()
    events=[]
    profiles=[(int(f),int(l),path) for f,l,path in (v.split(':',2) for v in a.profile)]
    if profiles:
        g.lib.sor_profile_stop.argtypes=[C.c_char_p]
    with (a.output/'trace.jsonl').open('w') as trace, ((a.output/'ram.bin').open('wb') if a.raw_ram else nullcontext()) as raw:
        for index,segment in enumerate(scenario['segments']):
            masks=[sum(1<<BUTTONS[b] for b in segment.get(k,[])) for k in ('p1','p2')]
            gate=segment.get('wait')
            for elapsed in range(segment['frames']+int(bool(gate))):
                if gate and ram[gate['address']]&gate.get('mask',255)==gate['value']:
                    events.append(dict(segment=index,frame=g.frame,idle_frames=elapsed));break
                if gate and elapsed==segment['frames']: raise RuntimeError(f'State gate {index} timed out')
                for first,_,_ in profiles:
                    if g.frame+1==first:g.lib.sor_profile_start()
                ram=g.step(*masks);trace.write(json.dumps(observation(ram,g.frame))+'\n')
                for _,last,path in profiles:
                    if g.frame==last and g.lib.sor_profile_stop(str(path).encode()):raise RuntimeError('profile write failed')
                if raw: raw.write(ram)
            if 'capture' in segment:g.capture(a.output/(segment['capture']+'.png'))
    (a.output/'events.json').write_text(json.dumps(events,indent=2)+'\n')
    (a.output/'metadata.json').write_text(json.dumps(dict(backend='Genesis Plus GX libretro',core=str(Path(a.core).resolve()),core_sha256=hashlib.sha256(Path(a.core).read_bytes()).hexdigest(),scenario_sha256=hashlib.sha256(Path(a.scenario).read_bytes()).hexdigest(),rom_sha256=hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),frames=g.frame,ram_first_frame=1,audio_sample_frames=g.sample_frames,audio_sample_rate=g.sample_rate),indent=2)+'\n')
    g.close()
if __name__=='__main__':main()
