#!/usr/bin/env bash
#
# What does the browser actually download, and where does the wasm's size go?
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/wasm-size/payload.sh
#
# Two questions, because they have different answers and different remedies:
# the payload is mostly resources, and the wasm is mostly engraving.

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

WEB="${WEB:-$HOME/build/mscz-wasm/web}"
[ -f "${WEB}/mscz-engine.wasm" ] || { echo "not built: ${WEB}" >&2; exit 1; }

sizes() {
    local f="$1" label="$2"
    printf '   %-26s %9d %9d %9d\n' "${label}" \
        "$(wc -c <"${f}")" \
        "$(gzip -9 -c "${f}" | wc -c)" \
        "$(brotli -q 11 -c "${f}" | wc -c)"
}

echo "== the download"
printf '   %-26s %9s %9s %9s\n' '' raw gzip brotli
sizes "${WEB}/mscz-engine.wasm" "mscz-engine.wasm"
sizes "${WEB}/mscz-engine.js" "mscz-engine.js (loader)"
sizes "${WEB}/mscz.js" "mscz.js (wrapper)"

python3 - "${WEB}" <<'PY'
import json, os, subprocess, sys

web = sys.argv[1]
index = json.load(open(os.path.join(web, "index.json")))

def compressed(path, tool):
    with open(path, "rb") as f:
        return len(subprocess.run(tool, stdin=f, stdout=subprocess.PIPE).stdout)

for group in ("core", "optional"):
    raw = gz = br = 0
    files = [r for r in index["resources"] if r["group"] == group]
    for r in files:
        p = os.path.join(web, r["url"])
        raw += os.path.getsize(p)
        gz += compressed(p, ["gzip", "-9", "-c"])
        br += compressed(p, ["brotli", "-q", "11", "-c"])
    print(f"   {group + ' resources (' + str(len(files)) + ')':<26} {raw:9d} {gz:9d} {br:9d}")
PY

echo
echo "== wasm sections"
"$(dirname "$(command -v emcc)")/../lib/binaryen/bin/wasm-objdump" -h \
    "${WEB}/mscz-engine.wasm" 2>/dev/null \
    | head -20 | sed 's/^/   /' \
    || llvm-objdump -h "${WEB}/mscz-engine.wasm" 2>/dev/null | sed 's/^/   /' \
    || echo "   (no wasm-objdump; skipping)"

echo
echo "== where the code is, by function name"
# wasm-opt knows each function's encoded size. Grouping the demangled names by
# namespace says which module to look at, which is what a size budget needs.
WASM_OPT=$(ls -d "$HOME"/.local/share/mise/installs/emsdk/*/upstream/bin/wasm-opt 2>/dev/null | head -1)
if [ ! -x "${WASM_OPT}" ]; then
    echo "   (no wasm-opt found; skipping)"
    exit 0
fi

# emcc names it after the JS output, not the wasm.
SYMBOLS="${WEB}/mscz-engine.js.symbols"
if [ ! -f "${SYMBOLS}" ]; then
    echo "   (no symbol map; link with --emit-symbol-map)"
    exit 0
fi

# wasm-opt knows each function's encoded size but, with the name section
# stripped, only by index. The symbol map supplies the names. --all-features
# because the module uses bulk memory, which wasm-opt will not accept by
# default.
"${WASM_OPT}" --all-features --func-metrics "${WEB}/mscz-engine.wasm" -o /dev/null 2>/dev/null \
  | awk '
    /^func: / { idx=$2; next }
    /^ \[binary-bytes\]/ { if (idx != "") { print idx, $3; idx="" } }
  ' >/tmp/mscz-func-bytes.txt

# Two numbering schemes have to be lined up. The symbol map indexes the whole
# wasm function space, imports included; wasm-opt counts only defined
# functions. So the offset is the number of imports -- which checks out exactly
# here: 11304 defined + 25 imported = 11329, and the map's last index is 11328.
#
# The files also need different separators, hence splitting by hand.
DEFINED=$(wc -l </tmp/mscz-func-bytes.txt)
MAXSYM=$(awk -F: '{ if ($1+0 > m) m = $1+0 } END { print m }' "${SYMBOLS}")
OFFSET=$(( MAXSYM + 1 - DEFINED ))
printf '   %d defined functions, %d imported\n' "${DEFINED}" "${OFFSET}"

awk -v off="${OFFSET}" '
  NR==FNR {
      p = index($0, ":")
      if (p) { name[substr($0, 1, p-1)] = substr($0, p+1) }
      next
  }
  {
      i = $1 + off
      print $2, ((i in name) ? name[i] : "(index " $1 ")")
  }
' "${SYMBOLS}" /tmp/mscz-func-bytes.txt \
  | sort -rn >/tmp/mscz-func-sizes.txt

total=$(awk '{s+=$1} END {print s}' /tmp/mscz-func-sizes.txt)
printf '   %d functions, %d bytes of function bodies\n\n' \
    "$(wc -l </tmp/mscz-func-sizes.txt)" "${total}"

# The symbol map carries demangled names, so grouping is by the leading
# namespace -- mu::engraving, muse, hb_/FT_ for the font libraries, the rest C
# and runtime.
echo "   by area, biggest first:"
awk '{
    size=$1
    $1=""
    n=substr($0, 2)
    if (n ~ /^mu::engraving::rendering/)  { key="mu::engraving::rendering" }
    else if (n ~ /^mu::engraving::read/)  { key="mu::engraving::read* (old formats)" }
    else if (n ~ /^mu::engraving::write/) { key="mu::engraving::write" }
    else if (n ~ /^mu::engraving/)        { key="mu::engraving::dom and the rest" }
    else if (n ~ /^muse::draw/)           { key="muse::draw" }
    else if (n ~ /^muse::/)               { key="muse (global)" }
    else if (n ~ /^mscz/ || n ~ /^\(anonymous namespace\)/) { key="our own code" }
    else if (n ~ /^(hb_|_hb_|OT::|AAT::|graph::)/) { key="HarfBuzz" }
    else if (n ~ /^(FT_|ft_|T1_|TT_|cff_|sfnt_|af_|ps_|psh_|tt_|cf2_|gxv)/) { key="FreeType" }
    else if (n ~ /^(inflate|deflate|crc32|adler32|_tr_|zcalloc|gz)/) { key="zlib" }
    else if (n ~ /^std::/)                { key="libc++ templates" }
    else                                  { key="C and runtime" }
    bytes[key]+=size; count[key]++
} END {
    for (k in bytes) { printf "   %10d  %6d fn  %s\n", bytes[k], count[k], k }
}' /tmp/mscz-func-sizes.txt | sort -rn

echo
echo "   biggest single functions:"
head -12 /tmp/mscz-func-sizes.txt | cut -c1-120 | sed 's/^/   /'
