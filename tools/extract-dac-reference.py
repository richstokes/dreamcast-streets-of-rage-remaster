#!/usr/bin/env python3
"""Extract the locked ROM's sound RAM image for local decoder comparisons."""
import argparse,hashlib
from pathlib import Path

def extract(rom):
    if hashlib.sha256(rom).hexdigest()!='dd44f120446654bb91c448762f3e0cd0d9b034f35d0e3266a4dc34402ada95c0':
        raise ValueError('Expected the locked SoR1 World rev00 ROM')
    position=0x795a2
    def byte():
        nonlocal position
        if position>=len(rom):raise ValueError('Truncated Kosinski stream')
        value=rom[position];position+=1;return value
    def word():return byte()|(byte()<<8)
    bits=word();remaining=16
    def bit():
        nonlocal bits,remaining
        value=bits&1;bits>>=1;remaining-=1
        if not remaining:bits=word();remaining=16
        return value
    output=bytearray()
    while True:
        if bit():output.append(byte());continue
        if not bit():
            length=(bit()<<1|bit())+2;distance=byte()-256
        else:
            lo,hi=byte(),byte();distance=(0xe000|((hi&0xf8)<<5)|lo)-65536
            length=hi&7
            if length:length+=2
            else:
                extension=byte()
                if extension==0:break
                if extension==1:continue
                length=extension+1
        if distance>=0 or -distance>len(output) or len(output)+length>8192:
            raise ValueError('Invalid sound-driver back reference')
        for _ in range(length):output.append(output[distance])
    if len(output)<0x1ec7 or len(output)>8192:raise ValueError('Unexpected sound-driver size')
    ram=bytearray(8192);ram[:0x1ec7]=output[:0x1ec7];ram[0x1ff8:0x1ffc]=bytes([0,0x80,7,0x80]);return ram
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(extract(a.rom.read_bytes()))
    print('Extracted local sound RAM; contains supplied game data, keep out of git.')
