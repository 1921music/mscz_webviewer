#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure saved OFF/EAS/TSF_SF3 web directories, in bytes (gzip level 9)."""
import argparse
import gzip
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path, help='directory containing OFF, EAS and TSF_SF3 web payloads')
a = p.parse_args()
print('| Backend | Engine WASM | Engine gzip | Synth WASM + bank | Synth + bank gzip | Standalone HTML |')
print('|---|---:|---:|---:|---:|---:|')
for backend in ('OFF', 'EAS', 'TSF_SF3'):
    root = a.directory / backend
    engine = (root / 'mscz-engine.wasm').read_bytes()
    audio = [path.read_bytes() for name in ('mscz-synth.wasm', 'sonivox.sf3')
             if (path := root / name).exists()]
    packed = lambda data: len(gzip.compress(data, compresslevel=9, mtime=0))
    values = (len(engine), packed(engine), sum(map(len, audio)), sum(map(packed, audio)),
              (root / 'index-standalone.html').stat().st_size)
    print(f'| {backend} | ' + ' | '.join(f'{n:,}' for n in values) + ' |')
