#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pack the built web payload into a single HTML that also works over file://.

    python3 scripts/gen-standalone-html.py --web build/web

Includes both core and optional resources. No bundler or Python packages are
needed. The source page and wrapper remain shared with the HTTP version.
"""
import argparse
import base64
import json
from pathlib import Path


def encoded(data):
    return base64.b64encode(data).decode("ascii")


def module_url(source):
    return "data:text/javascript;base64," + encoded(source.encode("utf-8"))


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"Expected exactly one standalone insertion point: {old}")
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    web = args.web.resolve()
    out = args.out or web / "index-standalone.html"

    engine = module_url((web / "mscz-engine.js").read_text(encoding="utf-8"))
    wrapper = replace_once((web / "mscz.js").read_text(encoding="utf-8"),
                           "from './mscz-engine.js'", f"from '{engine}'")
    html = (web / "index.html").read_text(encoding="utf-8")
    html = replace_once(html, "from './mscz.js'", f"from '{module_url(wrapper)}'")
    html = replace_once(html, "load({ base: '.' })", "load(embeddedOptions())")

    resources = []
    names = set()
    index = json.loads((web / "index.json").read_text(encoding="utf-8"))
    for entry in index["resources"]:
        path = (web / entry["url"]).resolve()
        path.relative_to(web)  # fail if the index points outside the payload
        if entry["name"] in names:
            raise ValueError(f"Duplicate resource: {entry['name']}")
        names.add(entry["name"])
        resources.append({"name": entry["name"], "data": encoded(path.read_bytes())})
    payload = {"wasm": encoded((web / "mscz-engine.wasm").read_bytes()), "resources": resources}
    # Escape raw HTML delimiters even if a resource alias later contains one.
    data = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).replace("<", "\\u003c")
    bootstrap = """
// All engine assets are in this document. Reading a score uses the File API.
function embeddedOptions () {
  const element = document.getElementById('mscz-embedded')
  const payload = JSON.parse(element.textContent)
  element.remove()
  const decode = value => Uint8Array.from(atob(value), c => c.charCodeAt(0))
  return {
    wasmBinary: decode(payload.wasm),
    resources: payload.resources.map(r => ({ name: r.name, bytes: decode(r.data) }))
  }
}
"""
    html = replace_once(html, '<script type="module">',
                        '<script id="mscz-embedded" type="application/json">' + data
                        + '</script>\n<script type="module">' + bootstrap)
    html = html.replace("Serve this directory over HTTP -- the module is an\n  ES module, so file:// will not do:",
                        "This generated standalone edition also opens directly via file://.\n  Source: web/index.html. Rebuild with scripts/build-wasm.sh. HTTP edition:")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding="utf-8", newline="\n")
    print(f"Standalone: {out} ({out.stat().st_size / 1024 / 1024:.2f} MiB, {len(resources)} resources)")


if __name__ == "__main__":
    main()
