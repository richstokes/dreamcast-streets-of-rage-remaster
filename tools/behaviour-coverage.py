#!/usr/bin/env python3
"""Behaviour the original exercises in each matched comparison window.

Reads the original's raw RAM captures (genesis_reference.py --raw-ram) and
counts, from the gate over the given number of frames: object types present,
enemy hits and knockouts, player grabs and throws (front $62, from behind $70),
weapons picked up and let go, food eaten (health gained), police specials
used, player health drops, hits between the two players with no enemy near,
continue prompts (player type $0F), continues taken, game overs (mode $0C) and
player 2 joining a game in progress.
Usage: tools/behaviour-coverage.py (fixed replay set, plus the
state-synchronised windows of tools/state-sync.py, from their sync frame over
the frames that matched).
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
    throws=collections.Counter(); pickups=collections.Counter(); released=0; food=0; friendly=0
    prompts=0; continues=0; game_overs=0; joins=0
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
            if w(r,0xFF00)==0x0C and w(prev,0xFF00)!=0x0C: game_overs+=1
            if r[0xB880]==0x01 and prev[0xB880]==0x00: joins+=1
            for p in players:
                if r[p]==0x0F and prev[p]!=0x0F: prompts+=1
                if r[p]==0x01 and prev[p]==0x0F: continues+=1
                if r[p]!=0x01 or prev[p]!=0x01: continue
                act=r[p+0x30]&0xFE
                if act!=prev[p+0x30]&0xFE and act in (0x62,0x70): throws['front' if act==0x62 else 'behind']+=1
                if r[p+0x60] and not prev[p+0x60]: pickups[hex(r[p+0x60])]+=1
                if prev[p+0x60] and not r[p+0x60]: released+=1
                if 0<w(prev,p+50)<w(r,p+50): food+=1
                other=players[1] if p==players[0] else players[0]
                if r[other]==0x01 and w(r,p+50)<w(prev,p+50):
                    near=lambda a:abs(w(r,p+16)-w(r,a+16))+abs(w(r,p+20)-w(r,a+20))
                    enemies=[0xB900+s*0x80 for s in range(66) if 0x20<=r[0xB900+s*0x80]<=0x2f or 0x55<=r[0xB900+s*0x80]<=0x58]
                    if abs(w(r,p+16)-w(r,other+16))<50 and abs(w(r,p+20)-w(r,other+20))<12 and all(near(e)>60 for e in enemies): friendly+=1
            if r[0xFF20]<prev[0xFF20]: lives_lost+=1
            for p in players:
                if r[p] and w(r,p+50)<w(prev,p+50): player_hits+=1
        prev=r
    return dict(frames=length,modes=dict(modes),waves=sorted(waves),object_types={hex(k):v for k,v in sorted(types.items())},grab_starts=grabs,grab_frames=grabframes,
                weapon_frames=weaponframes,weapons=sorted(hex(x) for x in weapons),police_specials_used=special,lives_lost=lives_lost,throws=dict(throws),weapon_pickups=dict(pickups),weapons_let_go=released,
                food_eaten=food,friendly_hits=friendly,continue_prompts=prompts,continues=continues,game_overs=game_overs,joins=joins,player_health_drops=player_hits,enemy_hits={hex(k):v for k,v in hits.items()},enemy_knockouts={hex(k):v for k,v in deaths.items()})
out={}
for name,path,gate,length in (('actions','build/genesis-phase-actions',1384,1481),('two-player','build/genesis-phase-two',1398,761),
                              ('round1-combat','build/genesis-round1',1384,3115),('round1-full','build/genesis-round1-full',1384,9976)):
    out[name]=analyse(name,path,gate,length)
for d,original in (('sync-17300','genesis-round1-full'),('sync-19200','genesis-round1-full'),('sync-22100','genesis-round1-full'),
                   ('sync-boss','bot-round1'),('sync-clear','bot-clear'),('sync-continue','bot-round1'),
                   ('sync-gameover','bot-gameover'),('sync-throws','bot-throws'),('sync-items','bot-items'),('sync-2p','bot-2p'),
                   ('sync-bat','bot-bat'),('sync-2p-continue','bot-2p-continue'),('sync-join','bot-join')):
    r=json.load(open('build/%s/result.json'%d))
    length=r['compared_frames'] if r['first_difference'] is None else r['first_difference']
    out[d]=analyse(d,'build/'+original,r['sync_frame'],length)
print(json.dumps(out,indent=1))
