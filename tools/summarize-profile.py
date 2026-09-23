#!/usr/bin/env python3
"""Summarize guest timing and displayed-frame counters from a Flycast serial log.

FRAME_STATS lines are CPU-loop intervals over 600-frame gameplay windows, with
the KOS VBlank and page-flip counters over the same window (later windows can
include idle time after a replay ends). GPU_STATS lines are renderer phase
times per 600 present calls, menus included.
"""
import argparse
import json
import re
from pathlib import Path


def summarize(text):
    frames = []
    phases = []
    phase_names = ('scene', 'wait', 'upload', 'commands', 'submit')
    for line in text.splitlines():
        if line.startswith('FRAME_STATS '):
            record = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
            if 'flips' in record and 'vblanks' in record:
                record['vblanks_without_flip'] = record['vblanks'] - record['flips']
            frames.append(record)
        elif line.startswith('GPU_STATS '):
            record = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)(?![\d/])', line)}
            for key in phase_names:
                match = re.search(rf'\b{key}=(\d+)/(\d+)', line)
                if match:
                    record.pop(key, None)
                    record[key + '_mean_us'], record[key + '_max_us'] = map(int, match.groups())
            record['ending_present_call'] = (len(phases) + 1) * 600
            phases.append(record)
    return {'gameplay_windows': frames, 'renderer_blocks': phases, 'audio': 'AUDIO frame=' in text}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    args = ap.parse_args()
    result = summarize(args.log.read_text(errors='replace'))
    if not result['gameplay_windows']:
        ap.error('No FRAME_STATS samples found; allow at least 600 gameplay intervals.')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
