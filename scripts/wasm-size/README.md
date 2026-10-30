# Measuring what the font libraries cost in wasm

These reproduce the numbers in `vendor/thirdparty/VENDOR.md`. Run them in the
`mscz_dev` distro, in this order:

```sh
bash scripts/wasm-size/native.sh        # the system shared libraries, for scale
bash scripts/wasm-size/wasm.sh          # FreeType, all modules
bash scripts/wasm-size/wasm-min.sh      # FreeType, CFF and TrueType only
bash scripts/wasm-size/isolate.sh       # subtract the libc baseline
bash scripts/wasm-size/harfbuzz-size.sh # add HarfBuzz with HB_TINY
```

`probe.c` is the measurement subject and doubles as the worked example of
`FT_Outline_Decompose` that the SVG provider's path mode needs: FreeType hands
back move/line/conic/cubic callbacks that map onto an SVG path's M/L/Q/C with
nothing in between but a divide by 64 and a sign flip on y.

`ftmodule-min.h` is the trimmed module list, and is what the real build should
use.

## What came out

Over a libc baseline of 22 KB raw / 10 KB brotli, `-Oz -flto`:

| | raw | brotli |
|---|---|---|
| FreeType, all modules | 572 KB | 209 KB |
| FreeType, CFF and TrueType only | 367 KB | 140 KB |
| HarfBuzz, `HB_TINY` | 216 KB | 76 KB |

So trimming FreeType's modules is worth 69 KB brotli, and the pair costs about
216 KB brotli. For scale, a page of score SVG is 5.6 KB brotli.

## Why not emscripten ports

`-sUSE_FREETYPE=1` fetches a tarball from GitHub, and an intercepting proxy can
truncate it. Resuming, HTTP/1.1, `Accept-Encoding: identity` and codeload
directly all fail the same way, so the sources are vendored under
`vendor/thirdparty/` instead. That also lets us pick the module set, which the
port would not.

## Why they cannot simply be dropped

Tempting, since the browser can rasterise fonts itself. But these are not only
for drawing: MuseScore's layout asks the font provider for every symbol's
bounding box and advance (`EngravingFont::computeMetrics`) and for text
advances, and those drive horizontal spacing. Today that comes from Qt; Phase 2
moves it onto FreeType and HarfBuzz. With no metrics source in the wasm the
engine cannot lay a score out at all.

Two ways around that were considered and not taken:

- Ask the browser, through synchronous calls out to Canvas `measureText` and
  `actualBoundingBox*`. Removes both libraries, but makes layout depend on
  browser font rasterisation, so it would differ between browsers and from
  desktop MuseScore, and the layout fingerprint would stop being a usable
  regression check.
- Precompute metrics tables. The SMuFL metadata already carries `glyphBBoxes`
  for every glyph -- 489 of them for Leland -- though MuseScore reads only
  `glyphsWithAnchors`, `glyphsWithAlternates` and `engravingDefaults` from it
  and takes bounding boxes from the font. Advances would have to be generated.
  Deterministic and would remove FreeType from the wasm, but text advances with
  kerning and ligatures are the hard part.
