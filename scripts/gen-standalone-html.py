#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pack the built web payload into a single HTML that also works over file://.

    python3 scripts/gen-standalone-html.py --web build/web

Includes both core and optional resources. No bundler or Python packages are
needed. The source page and wrapper remain shared with the HTTP version.
"""
import argparse
import base64
import gzip
import json
from pathlib import Path


def encoded(data):
    return base64.b64encode(data).decode("ascii")


def compressed(data):
    # Reproducible output; the browser consumes the standard gzip stream via
    # DecompressionStream. No runtime JavaScript compression library needed.
    return encoded(gzip.compress(data, compresslevel=9, mtime=0))


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
    text_fonts = module_url((web / "text-fonts.js").read_text(encoding="utf-8"))
    wrapper = replace_once((web / "mscz.js").read_text(encoding="utf-8"),
                           "from './mscz-engine.js'", f"from '{engine}'")
    wrapper = replace_once(wrapper, "from './text-fonts.js'", f"from '{text_fonts}'")
    html = (web / "index.html").read_text(encoding="utf-8")
    imports = 'load, SYMBOLS_FONT, SYMBOLS_PATH, FONTS_REFERENCE, FONTS_EMBED'
    html = replace_once(html, f"import {{ {imports} }} from './mscz.js'",
                        f"const {{ {imports} }} = await import(await embeddedModule())")
    html = replace_once(html, "load({ base: '.', textFonts })", "load({ ...await embeddedOptions(), textFonts })")

    resources = []
    names = set()
    index = json.loads((web / "index.json").read_text(encoding="utf-8"))
    for entry in index["resources"]:
        path = (web / entry["url"]).resolve()
        path.relative_to(web)  # fail if the index points outside the payload
        if entry["name"] in names:
            raise ValueError(f"Duplicate resource: {entry['name']}")
        names.add(entry["name"])
        resources.append({"name": entry["name"], "data": compressed(path.read_bytes())})
    payload = {"module": compressed(wrapper.encode("utf-8")),
               "wasm": compressed((web / "mscz-engine.wasm").read_bytes()), "resources": resources}
    if "import { attachPlayback }" in html:
        playback = module_url((web / "playback.js").read_text(encoding="utf-8"))
        ui = replace_once((web / "playback-ui.js").read_text(encoding="utf-8"),
                          "from './playback.js'", f"from '{playback}'")
        html = replace_once(html, "from './playback-ui.js'", f"from '{module_url(ui)}'")
        # A classic blob worker has no module fetches, including under file://
        # with networking disabled. Inline the generated ES module's factory.
        synth = replace_once((web / "mscz-synth.js").read_text(encoding="utf-8"),
                             'export default createSynth;', '')
        synth = synth.replace('import.meta.url', 'self.location.href')
        worker = replace_once((web / "playback-worker.js").read_text(encoding="utf-8"),
                              "import createSynth from './mscz-synth.js'", synth)
        payload['playback'] = {'worker': compressed(worker.encode()),
                               'wasm': compressed((web / 'mscz-synth.wasm').read_bytes())}
        if (web / 'sonivox.sf3').exists():
            payload['playback']['font'] = compressed((web / 'sonivox.sf3').read_bytes())
    # Escape raw HTML delimiters even if a resource alias later contains one.
    data = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).replace("<", "\\u003c")
    bootstrap = """
// All engine assets are in this document. Reading a score uses the File API.
let embeddedPayload
function readEmbeddedPayload () {
  if (!embeddedPayload) {
    const element = document.getElementById('mscz-embedded')
    embeddedPayload = JSON.parse(element.textContent)
    element.remove()
  }
  return embeddedPayload
}
async function decodeEmbedded (value) {
  const bytes = Uint8Array.from(atob(value), c => c.charCodeAt(0))
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}
async function embeddedModule () {
  const payload = readEmbeddedPayload()
  const bytes = await decodeEmbedded(payload.module)
  delete payload.module
  // Keep imports as data: modules, including on file:// with networking off.
  // Chunking avoids the argument-count limit of String.fromCharCode(...).
  let binary = ''
  for (let i = 0; i < bytes.length; i += 32768) {
    binary += String.fromCharCode(...bytes.subarray(i, i + 32768))
  }
  return 'data:text/javascript;base64,' + btoa(binary)
}
async function embeddedOptions () {
  const payload = readEmbeddedPayload()
  const wasmBinary = await decodeEmbedded(payload.wasm)
  delete payload.wasm
  const resources = []
  for (const resource of payload.resources) {
    resources.push({ name: resource.name, bytes: await decodeEmbedded(resource.data) })
    delete resource.data
  }
  let playbackAssets
  if (payload.playback) {
    playbackAssets = {
      worker: new TextDecoder().decode(await decodeEmbedded(payload.playback.worker)),
      wasm: await decodeEmbedded(payload.playback.wasm),
      font: payload.playback.font ? await decodeEmbedded(payload.playback.font) : null
    }
  }
  embeddedPayload = null
  return { wasmBinary, resources, playbackAssets }
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
