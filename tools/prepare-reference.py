#!/usr/bin/env python3
"""Stage the pinned PC runtime with the documented lockstep wakeup repair."""
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'research/StreetsOfRageProject/MegaDriveEnvironment'
target=ROOT/'build/reference-runtime-v2'
shutil.copytree(source,target,dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('.git','build','__pycache__'))
subprocess.run(['patch','--batch','-p1','-i',str(ROOT/'patches/reference-lockstep.patch')],
               cwd=target,check=True)
print(target)
