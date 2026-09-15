#!/usr/bin/env python3
"""Fetch exact research revisions; never fetch game ROMs or reset dirty trees."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def main():
    entries = json.loads((ROOT / 'tools/upstream-lock.json').read_text())['repositories']
    for item in entries:
        path = ROOT / item['path']
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'clone', item['url'], str(path)], check=True)
        if run('git', '-C', str(path), 'status', '--porcelain'):
            raise SystemExit(f'Refusing to change dirty research checkout: {path}')
        if run('git', '-C', str(path), 'rev-parse', 'HEAD') != item['revision']:
            subprocess.run(['git', '-C', str(path), 'fetch', 'origin', item['revision']], check=True)
            subprocess.run(['git', '-C', str(path), 'checkout', '--detach', item['revision']], check=True)
        subprocess.run(['git', '-C', str(path), 'submodule', 'update', '--init', '--recursive'], check=True)
        print(item['path'], item['revision'])

if __name__ == '__main__':
    main()
