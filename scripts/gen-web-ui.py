#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Select playback assets at configure time; OFF removes code, UI and stale assets."""
import argparse
from pathlib import Path
import re
import shutil

p = argparse.ArgumentParser()
p.add_argument('--web', type=Path, required=True)
p.add_argument('--backend', choices=['OFF', 'EAS', 'TSF_SF3'], required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
a.web.mkdir(parents=True, exist_ok=True)
for name in ('index.html', 'mscz.js', 'text-fonts.js'):
    text = (root / 'web' / name).read_text(encoding='utf-8')
    if a.backend == 'OFF':
        text = re.sub(r'<!-- PLAYBACK_BEGIN -->.*?<!-- PLAYBACK_END -->', '', text, flags=re.S)
        text = re.sub(r'// PLAYBACK_BEGIN.*?// PLAYBACK_END', '', text, flags=re.S)
    text = text.replace('@MSCZ_PLAYBACK@', a.backend)
    (a.web / name).write_text(text, encoding='utf-8')
for name in ('playback.js', 'playback-worker.js', 'playback-ui.js'):
    target = a.web / name
    if a.backend == 'OFF': target.unlink(missing_ok=True)
    else: shutil.copyfile(root / 'web' / name, target)
if a.backend == 'OFF':
    for name in ('mscz-synth.js', 'mscz-synth.wasm', 'playback-LICENSE.txt'):
        (a.web / name).unlink(missing_ok=True)
if a.backend == 'TSF_SF3': shutil.copyfile(root / 'assets/playback/sonivox.sf3', a.web / 'sonivox.sf3')
else: (a.web / 'sonivox.sf3').unlink(missing_ok=True)
if a.backend != 'OFF':
    paths = ['vendor/sonivox/LICENSE'] if a.backend == 'EAS' else ['vendor/tinysoundfont/LICENSE', 'vendor/stb/stb_vorbis.c', 'assets/playback/LICENSE', 'assets/playback/UPSTREAM.md']
    # stb carries its dual license at the end; no need to ship its entire source in HTML.
    notices = []
    for path in paths:
        text = (root / path).read_text(encoding='utf-8')
        if path.endswith('stb_vorbis.c'): text = text[text.rfind('ALTERNATIVE A'):]
        notices.append(path + '\n\n' + text)
    (a.web / 'playback-LICENSE.txt').write_text('\n\n'.join(notices), encoding='utf-8')
