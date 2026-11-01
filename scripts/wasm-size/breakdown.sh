#!/usr/bin/env bash
#
# Where do the four and a half megabytes go? Coarsely, and exactly.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/wasm-size/breakdown.sh
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/wasm-size/breakdown.sh --reuse
#
# Builds with LTO and the unity build off, which is the only way to get an
# answer that can be trusted. With LTO on, the linker merges functions across
# translation units and can say only which merged body a byte landed in, not
# which source file it came from -- which is how a 583KB __wasm_call_ctors and
# a 1.1MB "C and runtime" bucket turn up in a per-function listing and tell you
# nothing. With LTO and unity off there is one object per source file, and
# wasm-ld's map attributes every output byte to one of them.
#
# The totals come out larger than the shipping build's, because that is what
# LTO is for. The proportions are the point.

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-wasm-map}"
MAP="${BUILD_DIR}/mscz-engine.map"

if [ "${1:-}" != "--reuse" ] || [ ! -f "${MAP}" ]; then
    echo "== configuring without LTO or unity, so the map can attribute"
    emcmake cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DMUSE_QT_SUPPORT=OFF \
        -DMSCZ_WASM_LTO=OFF \
        -DMSCZ_USE_UNITY=OFF \
        >"${BUILD_DIR}.configure.log" 2>&1 || {
            echo "configure failed; see ${BUILD_DIR}.configure.log" >&2; exit 1; }

    echo "== building"
    start=$(date +%s)
    cmake --build "${BUILD_DIR}" -j "$(nproc)" >"${BUILD_DIR}.log" 2>&1 || {
        grep -E "error:" "${BUILD_DIR}.log" | head -10 >&2; exit 1; }
    echo "   $(( $(date +%s) - start )) s"
fi

[ -f "${MAP}" ] || { echo "no map at ${MAP}" >&2; exit 1; }

wasm="${BUILD_DIR}/web/mscz-engine.wasm"
printf '\n== this build: %s bytes raw, %s brotli (the shipping build is smaller; LTO)\n' \
    "$(wc -c <"${wasm}")" "$(brotli -q 11 -c "${wasm}" | wc -c)"

python3 -I - "${MAP}" <<'PY'
# wasm-ld's map has four columns -- Addr, Off, Size (hex), then a name whose
# indentation says what it is:
#
#    -  18c 1647 CODE                              indent 0: output section
#    -  18d    3         obj.o:(chunkname)         indent 8: input chunk
#    -  18d    3                 symbol            indent 16: symbol in it
#
# with one wrinkle: under DATA the segments .rodata, .data and .bss also sit at
# indent 0. .bss is excluded because it occupies no bytes in the file -- its
# Off column is 0 -- so it is not downloaded.
#
# The chunk lines are the ones to sum. They already carry the size, so adding
# up the symbols underneath would double count.

import collections, os, re, sys

SECTIONS = {
    "TYPE", "IMPORT", "FUNCTION", "TABLE", "MEMORY", "GLOBAL", "EXPORT",
    "ELEM", "CODE", "DATA", "DATACOUNT", "START", "TAG",
}

LINE = re.compile(r'^(\s*\S+)\s+(\S+)\s+([0-9a-f]+)\s(.*)$')

code = collections.Counter()
data = collections.Counter()
section = None
segment = None

for raw in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = LINE.match(raw.rstrip())
    if not m:
        continue
    try:
        size = int(m.group(3), 16)
    except ValueError:
        continue

    rest = m.group(4)
    indent = len(rest) - len(rest.lstrip())
    name = rest.strip()

    if indent == 0:
        if name in SECTIONS or name.startswith("CUSTOM("):
            section, segment = name, None
        elif name.startswith("."):
            segment = name
        continue

    if indent > 8:
        continue                      # a symbol inside a chunk

    obj = name.rsplit(":(", 1)[0] if ":(" in name else name

    if section == "CODE":
        code[obj] += size
    elif section == "DATA" and segment != ".bss":
        data[obj] += size


def area(path):
    """Coarse bucket. The map names inputs as "<archive>.a(<member>.o)", so the
    archive is what identifies the module -- there is no source path here."""
    archive = os.path.basename(path.split("(")[0])

    by_archive = {
        "libengraving.a": "engraving (the whole engine)",
        "libmuse_global.a": "muse/global",
        "libmuse_draw.a": "muse/draw",
        "libmscz_core.a": "our own code (SVG, hostio, font provider)",
        "libmscz_harfbuzz.a": "HarfBuzz (shaping + the subsetter)",
        "libfreetype.a": "FreeType",
        "libmscz_zlib.a": "zlib",
        "libemmalloc.a": "emmalloc",
        "libpugixml.a": "pugixml",
    }
    if archive in by_archive:
        return by_archive[archive]

    if archive.startswith("libc++"):
        return "libc++ (the C++ standard library)"
    if archive.startswith(("libcompiler_rt", "libclang_rt")):
        return "compiler runtime"
    if archive.startswith("lib"):
        return f"emscripten libc ({archive})"
    if path.startswith("<internal>"):
        return "linker-generated"
    if "mscz_wasm.dir" in path or "mscz_core.dir" in path:
        return "our own code (SVG, hostio, font provider)"
    return f"other: {archive or path}"


buckets, bcode, bdata = collections.Counter(), collections.Counter(), collections.Counter()
for obj, n in code.items():
    a = area(obj); buckets[a] += n; bcode[a] += n
for obj, n in data.items():
    a = area(obj); buckets[a] += n; bdata[a] += n

total = sum(buckets.values())
print(f"\n== {total} bytes ({total/1024:.0f} KB) attributed, "
      f"{sum(code.values())/1024:.0f} KB code + {sum(data.values())/1024:.0f} KB data\n")
print(f"   {'':<46}{'total':>9}{'code':>9}{'data':>9}   share")
for a, n in buckets.most_common():
    print(f"   {a:<46}{n:>9}{bcode[a]:>9}{bdata[a]:>9}   {100*n/total:5.1f}%")

both = collections.Counter()
for d in (code, data):
    for k, v in d.items():
        both[k] += v

print("\n   the twenty biggest single inputs:")
for obj, n in both.most_common(20):
    short = obj
    for cut in ("/vendor/aria/src/", "/vendor/thirdparty/", ".dir/", "/sysroot/lib/"):
        if cut in short:
            short = short.split(cut, 1)[1]
    print(f"   {n:>9}  {short[:84]}")

print("""
   Note: an archive stores its members by basename, so sources that share one
   -- engraving has three rw/*/tread.cpp -- are summed into a single line.""")
PY

# Inside engraving the map cannot go further, for the reason just printed. The
# object files on disk can: one per source file, with its real path. These are
# input sizes, before the linker drops what is unreachable, so they overstate
# -- but the shape of engraving is what is wanted here, not the exact bytes.
echo
echo "== inside the big archives, by directory (object files on disk, pre-link)"
python3 -I - "${BUILD_DIR}" <<'PY'
import collections, os, sys

root = sys.argv[1]
per_dir = collections.Counter()
per_file = collections.Counter()

for base, _dirs, files in os.walk(root):
    if ".dir" not in base:
        continue
    for f in files:
        if not f.endswith(".o"):
            continue
        path = os.path.join(base, f)
        size = os.path.getsize(path)

        # .../CMakeFiles/<target>.dir/<relative source path>.o
        head, _, rel = base.partition(".dir")
        target = os.path.basename(head)
        rel = rel.strip("/")

        if target == "engraving":
            parts = rel.split("/")
            if parts[0] == "rw" and len(parts) > 1:
                key = f"engraving/rw/{parts[1]}"
            elif parts[0] == "rendering" and len(parts) > 1:
                key = f"engraving/rendering/{parts[1]}"
            elif rel:
                key = f"engraving/{parts[0]}"
            else:
                key = "engraving/(top level)"
        else:
            key = target

        per_dir[key] += size
        per_file[os.path.join(target, rel, f) if rel else os.path.join(target, f)] += size

total = sum(per_dir.values())
print(f"\n   {total/1024:.0f} KB of object files\n")
for k, n in per_dir.most_common(22):
    print(f"   {k:<46}{n:>10}   {100*n/total:5.1f}%")

print("\n   the fifteen biggest single source files:")
for k, n in per_file.most_common(15):
    print(f"   {n:>10}  {k}")
PY
