#!/usr/bin/env python3
"""Lays the engine's resources out as files a browser can fetch, plus an index.

The engine asks for resources by the ":/..." names the .qrc files give them,
and in the browser those names are answered by JS pushing bytes in (see
src/mscz_wasm.cpp). This writes one file per resource and an index.json saying
what each is called and how big it is, so the loader can fetch what it needs
without knowing the engine's naming.

    gen-web-resources.py --manifest resources.manifest --root <tree> --out <dir>

Resources are grouped so the loader can tell what it must have before the first
render from what can wait:

  core      metadata, instrument definitions, chord and style descriptions, and
            the fonts a default-style score uses. The engine cannot lay out a
            page without these.
  optional  everything a score has to ask for before it is read: alternative
            music fonts, tablature, figured bass, and the fretboard diagram
            table.

The fretboard table is the single largest resource at 2.5MB, and it is read
lazily -- FretDiagram asks for it the first time one is laid out (dom/fret.cpp)
-- so a score without fret diagrams never touches it. Fonts cannot be deferred
the same way, because FontsDatabase::addFont checks the file exists when the
family is registered, which happens once at startup.
"""

import argparse
import json
import os
import shutil
import sys

# What a default-style score needs. Leland is the music font MuseScore 4 ships
# with and Edwin the text one; the SMuFL metadata is what maps symbol names to
# code points, and the instrument and chord data is read while loading.
CORE_PREFIXES = (
    "fonts/smufl/",
    "fonts/leland/",
    "fonts/musejazz/",
    "fonts/edwin/",
    "engraving/instruments/",
    "engraving/styles/",
)


def group(alias):
    return "core" if alias.startswith(CORE_PREFIXES) else "optional"


def read_manifest(path):
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n").rstrip("\r")
        if not line or line.startswith("#"):
            continue
        alias, _, real = line.partition("\t")
        if real:
            yield alias, real


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    out_files = os.path.join(args.out, "resources")
    if os.path.isdir(out_files):
        shutil.rmtree(out_files)
    os.makedirs(out_files, exist_ok=True)

    entries = []
    for alias, real in sorted(read_manifest(args.manifest)):
        src = os.path.join(args.root, real)
        if not os.path.isfile(src):
            print(f"warning: missing resource {alias} -> {src}", file=sys.stderr)
            continue

        # The alias is already a relative path with no ".." in it, and keeping
        # it as the URL means the index needs no second naming scheme.
        dst = os.path.join(out_files, alias)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)

        entries.append({
            "name": ":/" + alias,
            "url": "resources/" + alias,
            "bytes": os.path.getsize(src),
            "group": group(alias),
        })

    index = {"resources": entries}
    with open(os.path.join(args.out, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=1, sort_keys=True)
        f.write("\n")

    for name in ("core", "optional"):
        part = [e for e in entries if e["group"] == name]
        print(f"{name:9s} {len(part):3d} files  {sum(e['bytes'] for e in part) / 1024:8.0f} KB")


if __name__ == "__main__":
    main()
