#!/usr/bin/env python3
"""Inspect a user-supplied ROM: header, checksum and whether it is the supported dump."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

# Streets of Rage / Bare Knuckle (World), revision 00: the only dump the generated
# code and the reference comparisons are validated against.
LOCKED_SHA256 = 'dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0'

def inspect(data):
    if len(data) != 0x80000:
        raise ValueError('Expected 524288 bytes, raw big-endian, no copier header or interleaving')
    if not data[0x100:0x110].startswith(b'SEGA MEGA DRIVE'):
        raise ValueError('Missing Mega Drive header; byte-swapped/interleaved images are unsupported')
    if data[0x180:0x18e].strip() != b'MK 00001019-00':
        raise ValueError('Expected SoR1 JUE revision 00 product MK 00001019-00')
    if data[0x1f0:0x200].strip() != b'JUE':
        raise ValueError('Expected JUE region header; initial execution target is overseas NTSC')
    stored = struct.unpack_from('>H', data, 0x18e)[0]
    calculated = sum(struct.unpack('>' + 'H' * ((len(data)-512)//2), data[512:])) & 0xffff
    if stored != 0x9409 or calculated != stored:
        raise ValueError(f'Expected checksum 9409; header={stored:04x}, calculated={calculated:04x}')
    sha256 = hashlib.sha256(data).hexdigest()
    return dict(size=len(data), sha256=sha256, known=sha256 == LOCKED_SHA256,
                product='MK 00001019-00', region='JUE', execution_region='overseas NTSC',
                checksum=f'{stored:04x}')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--require-known', action='store_true')
    a = p.parse_args()
    try:
        result = inspect(a.rom.read_bytes())
        if a.require_known and not result['known']:
            raise ValueError(f'ROM SHA-256 {result["sha256"]} is not the supported dump {LOCKED_SHA256}')
    except (OSError, ValueError) as e:
        p.exit(1, f'{e}\n')
    text = json.dumps(result, indent=2) + '\n'
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(text)
    print(text, end='')

if __name__ == '__main__':
    main()
