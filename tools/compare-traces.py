#!/usr/bin/env python3
"""Compare the observations of two runs frame number by frame number."""
import argparse
import json
from pathlib import Path

FIELDS=('mode','stage','wave','camera','p1_lives','p2_lives','actors')
def load(path):
    rows=[json.loads(line) for line in Path(path).read_text().splitlines()]
    result={r['frame']:r for r in rows}
    if len(result)!=len(rows): raise ValueError('Duplicate frame in trace')
    return result

def compare(left,right):
    common=sorted(left.keys() & right.keys())
    output=dict(common_frames=len(common),left_only=len(left.keys()-right.keys()),
                right_only=len(right.keys()-left.keys()),fields={})
    for field in (*FIELDS,'ram_sha256'):
        mismatches=[f for f in common if left[f][field]!=right[f][field]]
        output['fields'][field]=dict(mismatched_frames=len(mismatches),
            first_frame=mismatches[0] if mismatches else None)
    output['equal']=bool(common) and not output['left_only'] and not output['right_only'] and all(
        not d['mismatched_frames'] for d in output['fields'].values())
    return output

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('left');ap.add_argument('right');ap.add_argument('--output',type=Path)
    ap.add_argument('--require-equal',action='store_true');args=ap.parse_args()
    result=compare(load(args.left),load(args.right));text=json.dumps(result,indent=2)+'\n'
    if args.output: args.output.write_text(text)
    print(text,end='')
    if args.require_equal and not result['equal']: raise SystemExit(1)
if __name__=='__main__':main()
