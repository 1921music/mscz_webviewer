#!/usr/bin/env bash
#
# Vendors the third-party font libraries the wasm build needs, which the
# emscripten ports cannot supply where TLS is intercepted: an intercepting
# proxy can truncate a GitHub tarball, so `emcc -sUSE_FREETYPE=1` cannot fetch
# anything. Vendoring also lets us pick FreeType's module set, which is worth
# 69KB brotli on its own.
#
#   FreeType  from the copy aria bundles
#   HarfBuzz  from a release tarball, passed in or found in Downloads
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/vendor-thirdparty.sh [harfbuzz.tar.xz]
#
# Measured cost of the two, over a libc baseline, -Oz -flto:
#   FreeType, CFF and TrueType only   367 KB raw   140 KB brotli
#   HarfBuzz, HB_TINY                 216 KB raw    76 KB brotli
#
# Nothing is run from inside the unpacked archive; paths are passed in.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
DEST="${REPO_DIR}/vendor/thirdparty"
HB_TARBALL="${1:-}"

if [ -z "${HB_TARBALL}" ]; then
    HB_TARBALL=$(ls -t /mnt/c/Users/*/Downloads/harfbuzz-*.tar.xz 2>/dev/null | head -1 || true)
fi

mkdir -p "${DEST}"

# --- FreeType -----------------------------------------------------------------

FT_SRC="${ARIA_DIR}/src/framework/draw/thirdparty/freetype"
FT_DIR=$(ls -d "${FT_SRC}"/freetype-* 2>/dev/null | head -1 || true)
if [ -z "${FT_DIR}" ]; then
    echo "FreeType source not found under ${FT_SRC}" >&2
    exit 1
fi
FT_VERSION=$(basename "${FT_DIR}")

echo "== FreeType: ${FT_VERSION}"
rm -rf "${DEST}/freetype"
# docs is 7MB of HTML, and we build with CMake so the autotools and other
# build systems are dead weight.
rsync -a \
    --exclude='docs/' --exclude='objs/' --exclude='tests/' \
    --exclude='builds/windows/' --exclude='builds/mac/' --exclude='builds/amiga/' \
    --exclude='builds/wince/' --exclude='builds/vms/' --exclude='builds/dos/' \
    --exclude='builds/os2/' --exclude='builds/ansi/' --exclude='builds/atari/' \
    --exclude='builds/beos/' --exclude='builds/symbian/' \
    --exclude='ChangeLog*' --exclude='*.vcxproj*' --exclude='*.dsp' --exclude='*.dsw' \
    "${FT_DIR}/" "${DEST}/freetype/"
printf '   %s KB\n' "$(du -sk "${DEST}/freetype" | cut -f1)"

# --- HarfBuzz -----------------------------------------------------------------

if [ -z "${HB_TARBALL}" ] || [ ! -f "${HB_TARBALL}" ]; then
    echo
    echo "No HarfBuzz tarball given or found in Downloads; skipping HarfBuzz." >&2
    echo "Pass one as the first argument." >&2
else
    echo
    echo "== HarfBuzz: $(basename "${HB_TARBALL}")"
    if ! xz -t "${HB_TARBALL}" 2>/dev/null; then
        echo "   archive is truncated or not xz" >&2
        exit 1
    fi
    printf '   sha256 %s\n' "$(sha256sum <"${HB_TARBALL}" | cut -d' ' -f1)"

    STAGE=$(mktemp -d)
    trap 'rm -rf "${STAGE}"' EXIT
    tar -xJf "${HB_TARBALL}" -C "${STAGE}" --strip-components=1

    # src/ holds the amalgam (src/harfbuzz.cc) and everything it includes. The
    # rest of the tree is tests, fuzzers, docs, test fonts and bindings -- about
    # 100MB of it.
    rm -rf "${DEST}/harfbuzz"
    mkdir -p "${DEST}/harfbuzz/src"
    rsync -a --prune-empty-dirs \
        --exclude='test/' --exclude='tests/' --exclude='fuzzing/' \
        --exclude='*test*.cc' --exclude='*fuzzer*' --exclude='*-benchmark*' \
        --include='*/' --include='*.cc' --include='*.hh' --include='*.h' \
        --include='*.py' --include='*.txt' --exclude='*' \
        "${STAGE}/src/" "${DEST}/harfbuzz/src/"
    for f in COPYING AUTHORS NEWS README.md; do
        [ -f "${STAGE}/${f}" ] && cp "${STAGE}/${f}" "${DEST}/harfbuzz/${f}"
    done
    printf '   %s KB\n' "$(du -sk "${DEST}/harfbuzz" | cut -f1)"
fi

# --- zlib ---------------------------------------------------------------------

# .mscz is a zip, so inflate is not optional. The emscripten port cannot be
# fetched (see the note at the top), and neither can zlib.net's tarball: where
# TLS is intercepted a large HTTPS download can come back truncated -- the tail
# is silently dropped, and a Range request for it returns the same truncated
# body.
#
# apt is unaffected, because it talks plain HTTP to the archive, so zlib comes
# through it. That also means the Ubuntu-patched source, which carries the
# current CVE fixes.
echo
echo "== zlib: via apt-get source"

SRC_LIST=/etc/apt/sources.list.d/ubuntu.sources
if [ -f "${SRC_LIST}" ] && ! grep -qE "^Types:.*deb-src" "${SRC_LIST}"; then
    echo "   enabling deb-src"
    sudo sed -i -E 's/^Types: deb[[:space:]]*$/Types: deb deb-src/' "${SRC_LIST}"
    sudo apt-get -o Acquire::Retries=5 update -qq
fi

ZSTAGE=$(mktemp -d)
(cd "${ZSTAGE}" && apt-get source zlib >/dev/null 2>&1)
ZDIR=$(ls -d "${ZSTAGE}"/zlib-* 2>/dev/null | head -1 || true)

if [ -z "${ZDIR}" ]; then
    echo "   could not get zlib source; is deb-src enabled?" >&2
    ZLIB_VERSION="not vendored"
else
    ZLIB_VERSION=$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "${ZDIR}/zlib.h" | head -1)
    echo "   ${ZLIB_VERSION} (${ZDIR##*/})"

    # Only the library itself. No contrib, no test, no gz* -- the zip reader
    # uses the raw inflate/deflate interface, not the gzip file wrappers --
    # though gz* is kept because zlib.h declares it and dropping it would mean
    # editing the header.
    rm -rf "${DEST}/zlib"
    mkdir -p "${DEST}/zlib"
    for f in adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c \
             gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c \
             uncompr.c zutil.c \
             crc32.h deflate.h gzguts.h inffast.h inffixed.h inflate.h \
             inftrees.h trees.h zconf.h zlib.h zutil.h \
             README LICENSE; do
        [ -f "${ZDIR}/${f}" ] && cp "${ZDIR}/${f}" "${DEST}/zlib/${f}"
    done
    printf '   %s KB\n' "$(du -sk "${DEST}/zlib" | cut -f1)"
fi
rm -rf "${ZSTAGE}"

# --- manifest -----------------------------------------------------------------

cat >"${DEST}/VENDOR.md" <<EOF
# Vendored third-party libraries

Generated by \`scripts/vendor-thirdparty.sh\`.

Emscripten ports would normally supply these, but **where TLS is intercepted a
large HTTPS download can come back truncated** -- the tail is silently dropped,
and a Range request for it returns the same truncated body.
So \`emcc -sUSE_FREETYPE=1\` and \`-sUSE_ZLIB=1\` cannot fetch anything.
Vendoring also lets us choose FreeType's module set.

apt is unaffected, because it talks plain HTTP to the archive, which is why
zlib comes through \`apt-get source\` rather than from zlib.net.

| | Version | Source | License |
|---|---|---|---|
| FreeType | ${FT_VERSION#freetype-} | the copy bundled in aria | FTL or GPLv2 |
| HarfBuzz | $([ -n "${HB_TARBALL}" ] && basename "${HB_TARBALL}" | sed 's/harfbuzz-//; s/\.tar\.xz//' || echo "not vendored") | release tarball | MIT-style ("Old MIT") |
| zlib | ${ZLIB_VERSION} | \`apt-get source zlib\` (Ubuntu-patched) | zlib |

Copied $(date -u +%Y-%m-%dT%H:%M:%SZ).

## Why the font libraries are needed

They are not only for drawing. MuseScore's layout asks the font provider for
every symbol's bounding box and advance (\`EngravingFont::computeMetrics\`) and
for text advances, and those drive the horizontal spacing. Phase 2 moves that
off Qt, and these are what it moves onto. Dropping them would mean the engine
could not lay a score out at all.

## Measured wasm cost

Over a libc baseline, \`-Oz -flto\`:

| | raw | brotli |
|---|---|---|
| FreeType, all modules | 572 KB | 209 KB |
| FreeType, CFF and TrueType only | 367 KB | 140 KB |
| HarfBuzz, \`HB_TINY\` | 216 KB | 76 KB |

So about 216 KB brotli for both, trimmed. A page of score SVG is 5.6 KB
brotli, so this is roughly 39 pages' worth, paid once and cached.

## Configuration

- FreeType: modules trimmed to sfnt, CFF and TrueType plus the PostScript
  helpers CFF charstrings need. No Type1, CID, PFR, Windows FNT, PCF, BDF,
  OT-SVG, autofitter, or rasterisers -- we never rasterise, the output is SVG.
- HarfBuzz: \`HB_TINY\`, built from the amalgam \`src/harfbuzz-subset.cc\` plus
  \`src/hb-ft.cc\`, rather than its 142 sources. Keeps the OpenType shaper,
  which is the part MuseScore uses (\`hb_ft_font_create\`, \`hb_shape\` with
  kern/liga/clig/dlig/hlig). Note that \`harfbuzz-subset.cc\` is very nearly a
  superset of \`harfbuzz.cc\`, not a companion to it, so the two cannot both be
  linked; what it lacks is the FreeType bridge, hence the one extra file.
- zlib: the library sources only, no \`contrib/\` and no tests.
EOF

echo
echo "== total"
du -sh "${DEST}"
du -sh "${DEST}"/*/ 2>/dev/null | sort -rh
