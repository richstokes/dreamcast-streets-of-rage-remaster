#!/usr/bin/env python3
"""Extract the Z80 sound driver from the ROM as a sound-RAM image, for the DAC driver tests.

The driver is Kosinski-compressed in the cartridge; the game decompresses it
into Z80 RAM and writes the DAC state words at $1FF8 before starting the Z80."""
import argparse
import hashlib
from pathlib import Path
from rom import LOCKED_SHA256

DRIVER_SOURCE=0x795a2          # the compressed driver in the ROM
DRIVER_SIZE=0x1ec7             # decompressed
DAC_STATE=bytes([0,0x80,7,0x80])   # $1FF8-$1FFB as the game initialises them

def extract(rom):
    if hashlib.sha256(rom).hexdigest()!=LOCKED_SHA256:
        raise ValueError('Expected the locked SoR1 World rev00 ROM')
    position=DRIVER_SOURCE
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
    if len(output)<DRIVER_SIZE or len(output)>8192:raise ValueError('Unexpected sound-driver size')
    ram=bytearray(8192);ram[:DRIVER_SIZE]=output[:DRIVER_SIZE];ram[0x1ff8:0x1ffc]=DAC_STATE;return ram
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(extract(a.rom.read_bytes()))
