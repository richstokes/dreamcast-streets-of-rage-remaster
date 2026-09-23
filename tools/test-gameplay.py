#!/usr/bin/env python3
"""Check that the native port repeats itself and walks like the original in the boot-movement scenario."""
import argparse
import json
from pathlib import Path

def load(path):return {row['frame']:row for row in map(json.loads,Path(path).read_text().splitlines())}
def player(row):return next(actor for actor in row['actors'] if actor['slot']==0xb800)
def walk(rows,start,end):
    values=[player(rows[f])['x'] for f in range(start,end+1)]
    return [b-a for a,b in zip(values,values[1:]) if a!=b]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original');ap.add_argument('native');ap.add_argument('repeat')
    args=ap.parse_args();original,native,repeat=map(load,(args.original,args.native,args.repeat))
    assert native and native==repeat,'Native replay is not repeatable'
    # Frame numbers belong to the locked boot-movement scenario, not arbitrary input.
    a,b=walk(original,1384,1444),walk(native,1384,1444)
    assert a and a==b,('Movement integration differs',a,b)
    assert player(original[1444])==player(native[1444]),'Walking endpoint differs'
    print(f'PASS: {len(native)} identical native snapshots; {len(a)} walking updates match original increments and endpoint')
if __name__=='__main__':main()
