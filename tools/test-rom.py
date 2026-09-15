#!/usr/bin/env python3
"""Bounds tests use synthetic bytes; never include a game dump in tests."""
from rom import inspect
for candidate in (b'',b'\0'*512,b'\0'*524287,b'\0'*524288,b'\0'*524800):
    try: inspect(candidate)
    except ValueError: pass
    else: raise AssertionError('Invalid ROM accepted')
print('ROM: empty, truncated, oversized and invalid headers rejected')
