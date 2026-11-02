#!/usr/bin/env python3
"""Copy the matching upstream MusicXML importer without changing its checkout.

python3 scripts/vendor-musicxml.py --source ../MuseScore
This overwrites only the listed MusicXML files, not the rest of vendor/aria.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--ref', default='v4.7.5')
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
source = args.source.resolve()

def git(*arguments):
    return subprocess.check_output(['git', '-c', f'safe.directory={source.as_posix()}',
                                    '-C', str(source), *arguments])

prefix = 'src/importexport/musicxml/'
paths = git('ls-tree', '-r', '--name-only', args.ref, '--', prefix).decode().splitlines()
hashes = {}
for name in paths:
    if not name.endswith(('.cpp', '.h')):
        continue
    if not (name.startswith(prefix + 'internal/import/')
            or name.startswith(prefix + 'internal/shared/')
            or name in [prefix + 'imusicxmlconfiguration.h',
                        prefix + 'internal/musicxmlconfiguration.cpp',
                        prefix + 'internal/musicxmlconfiguration.h']):
        continue
    data = git('show', f'{args.ref}:{name}')
    hashes[name.removeprefix(prefix)] = hashlib.sha256(data).hexdigest()
    if name == prefix + 'imusicxmlconfiguration.h':
        # Upstream relies on a precompiled/transitive include for std::optional.
        data = data.replace(b'#pragma once\n', b'#pragma once\n\n#include <optional>\n', 1)
    target = repo / 'vendor/aria' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    # vendor/** is marked -text; match the existing vendored headers' CRLF.
    target.write_bytes(data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))

provenance = {
    'upstream': 'https://github.com/musescore/MuseScore.git',
    'ref': args.ref,
    'commit': git('rev-parse', f'{args.ref}^{{commit}}').decode().strip(),
    'line_endings': 'Files use CRLF to match vendor/aria; upstream hashes use LF.',
    'upstream_sha256': hashes,
    'local_changes': {'imusicxmlconfiguration.h': 'Include <optional> explicitly; no PCH.'},
}
destination = repo / 'vendor/aria' / prefix
(destination / 'SOURCE.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
print(f'Imported {len(hashes)} files from {args.ref} ({provenance["commit"]}).')
