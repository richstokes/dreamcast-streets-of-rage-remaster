#!/usr/bin/env python3
"""Behaviour the original exercises in each matched comparison window.

Reads the original's raw RAM captures (genesis_reference.py --raw-ram) and
counts, from the gate over the given number of frames: object types present,
enemy hits and knockouts, player grabs, held weapons, police specials used and
player health drops. Usage: tools/behaviour-coverage.py (fixed replay set,
plus the state-synchronised windows of tools/state-sync.py, from their sync
frame over the frames that matched).
"""
import mmap,json,collections,sys
def ram(p,first):
    f=open(p+'/ram.bin','rb');m=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
    return lambda fr: m[(fr-first)*65536:(fr-first+1)*65536]
def w(r,a): return (r[a]<<8)|r[a+1]
players=(0xB800,0xB880)
def analyse(name,path,gate,length):
    R=ram(path,1)
    types=collections.Counter(); hits=collections.Counter(); deaths=collections.Counter()
    modes=collections.Counter(); waves=set(); lives_lost=0
    grabs=0; grabframes=0; weaponframes=0; weapons=set(); special=0; lives=[]; player_hits=0
    prev=None
    for rel in range(length):
        r=R(gate+rel)
        modes['%02x'%w(r,0xFF00)]+=1; waves.add(w(r,0xFF04))
        # object slots $B900.. (0x80 each, 66 slots)
        cur={}
        for s in range(66):
            base=0xB900+s*0x80; t=r[base]
            if t: cur[s]=(t,r[base+0x32] if False else w(r,base+0x30))
            if t: types[t]+=1
        for p in players:
            g=w(r,p+76)
            if g: grabframes+=1
            if prev is not None and g and not w(prev,p+76): grabs+=1
            wp=r[p+0x60]
            if wp: weaponframes+=1; weapons.add(wp)
        if prev is not None:
            for s in range(66):
                base=0xB900+s*0x80; t=r[base]
                if t and prev[base]==t and (0x21<=t<=0x2f or 0x55<=t<=0x58):
                    h0=w(prev,base+50); h1=w(r,base+50)
                    if h1<h0: hits[t]+=1
                    if h0 and not h1: deaths[t]+=1
            if r[0xFF21]<prev[0xFF21]: special+=1
            if r[0xFF20]<prev[0xFF20]: lives_lost+=1
            for p in players:
                if r[p] and w(r,p+50)<w(prev,p+50): player_hits+=1
        prev=r
    return dict(frames=length,modes=dict(modes),waves=sorted(waves),object_types={hex(k):v for k,v in sorted(types.items())},grab_starts=grabs,grab_frames=grabframes,
                weapon_frames=weaponframes,weapons=sorted(hex(x) for x in weapons),police_specials_used=special,lives_lost=lives_lost,player_health_drops=player_hits,enemy_hits={hex(k):v for k,v in hits.items()},enemy_knockouts={hex(k):v for k,v in deaths.items()})
out={}
for name,path,gate,length in (('actions','build/genesis-phase-actions',1384,1481),('two-player','build/genesis-phase-two',1398,761),
                              ('round1-combat','build/genesis-round1',1384,3115),('round1-full','build/genesis-round1-full',1384,9976)):
    out[name]=analyse(name,path,gate,length)
for d,original in (('sync-17300','genesis-round1-full'),('sync-19200','genesis-round1-full'),('sync-22100','genesis-round1-full'),
                   ('sync-boss','bot-round1'),('sync-clear','bot-clear'),('sync-gameover','bot-round1')):
    r=json.load(open('build/%s/result.json'%d))
    length=r['compared_frames'] if r['first_difference'] is None else r['first_difference']
    out[d]=analyse(d,'build/'+original,r['sync_frame'],length)
print(json.dumps(out,indent=1))
