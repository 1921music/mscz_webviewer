#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wrap the source-controlled CJK/text regression score in an mscz ZIP."""
import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=Path('build/browser-text-fixture/BrowserText.mscz'))
args = parser.parse_args()
source = Path(__file__).resolve().parent.parent / 'test/browser_text.mscx'
args.out.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(args.out, 'w', compression=ZIP_DEFLATED) as archive:
    archive.write(source, 'BrowserText.mscx')
print(args.out.resolve())
