#!/usr/bin/env python3
"""Compare two runs of a scenario from the frame each passed a state gate.

The observations of every frame after the gate are compared field by field,
then the active objects' RAM region by region (sor_ram.OBJECT_REGIONS).
"""
import argparse
import json
import mmap
from pathlib import Path

from sor_ram import OBJECT_REGIONS as REGIONS

FIELDS=('mode','stage','wave','camera','p1_lives','p2_lives','actors')


def read(directory,segment):
    events=json.loads((directory/'events.json').read_text())
    anchors=[v['frame'] for v in events if v['segment']==segment]
    if len(anchors)!=1: raise ValueError('Exactly one matching gate event is required')
    rows={v['frame']:v for v in map(json.loads,(directory/'trace.jsonl').read_text().splitlines())}
    return anchors[0],rows,json.loads((directory/'metadata.json').read_text())


def compare(left,right,segment):
    a,lr,lm=read(left,segment);b,rr,rm=read(right,segment)
    lhash=lm.get('rom_sha256',lm.get('rom',{}).get('sha256'))
    rhash=rm.get('rom_sha256',rm.get('rom',{}).get('sha256'))
    if not lhash or lhash!=rhash:raise ValueError('ROM identities differ or are missing')
    if not lm.get('scenario_sha256') or lm['scenario_sha256']!=rm.get('scenario_sha256'):raise ValueError('Scenario identities differ or are missing')
    length=min(max(lr)-a,max(rr)-b)+1
    if length<=0:raise ValueError('No observations after the anchor')
    result=dict(rom_sha256=lhash,scenario_sha256=lm['scenario_sha256'],segment=segment,left_anchor=a,right_anchor=b,compared_frames=length,
                left_remaining=max(lr)-a+1,right_remaining=max(rr)-b+1,fields={})
    for field in FIELDS:
        bad=[i for i in range(length) if lr[a+i][field]!=rr[b+i][field]]
        result['fields'][field]=dict(mismatched_frames=len(bad),first_relative_frame=bad[0] if bad else None)
    result['observations_equal']=result['left_remaining']==result['right_remaining'] and all(v['mismatched_frames']==0 for v in result['fields'].values())
    result['active_object_regions']={key:dict(mismatched_frames=0,first_relative_frame=None) for key in REGIONS}
    with (left/'ram.bin').open('rb') as lf,(right/'ram.bin').open('rb') as rf:
        with mmap.mmap(lf.fileno(),0,access=mmap.ACCESS_READ) as lram,mmap.mmap(rf.fileno(),0,access=mmap.ACCESS_READ) as rram:
            lfirst=lm['ram_first_frame'];rfirst=rm['ram_first_frame']
            for i in range(length):
                l=lram[(a+i-lfirst)*65536:(a+i-lfirst+1)*65536]
                r=rram[(b+i-rfirst)*65536:(b+i-rfirst+1)*65536]
                if len(l)!=65536 or len(r)!=65536:raise ValueError('Truncated RAM trace')
                slots={v['slot'] for v in lr[a+i]['actors']}|{v['slot'] for v in rr[b+i]['actors']}
                for name,(start,end) in REGIONS.items():
                    if any(l[s+start:s+end]!=r[s+start:s+end] for s in slots):
                        out=result['active_object_regions'][name];out['mismatched_frames']+=1
                        if out['first_relative_frame'] is None:out['first_relative_frame']=i
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('left',type=Path);ap.add_argument('right',type=Path)
    ap.add_argument('--segment',type=int,required=True);ap.add_argument('--require-observations-equal',action='store_true')
    args=ap.parse_args();result=compare(args.left,args.right,args.segment)
    print(json.dumps(result,indent=2))
    if args.require_observations_equal and not result['observations_equal']:raise SystemExit(1)

if __name__=='__main__':main()
