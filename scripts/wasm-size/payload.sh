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

SYMBOLS="${WEB}/mscz-engine.wasm.symbols"
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

# symbol map is "index:name"
awk -F: 'NR==FNR { name[$1]=$2; next } { print $2, (($1 in name) ? name[$1] : "(index " $1 ")") }' \
    "${SYMBOLS}" /tmp/mscz-func-bytes.txt \
  | sort -rn >/tmp/mscz-func-sizes.txt

total=$(awk '{s+=$1} END {print s}' /tmp/mscz-func-sizes.txt)
printf '   %d functions, %d bytes of function bodies\n\n' \
    "$(wc -l </tmp/mscz-func-sizes.txt)" "${total}"

echo "   by top-level name, biggest first:"
awk '{
    n=$2
    sub(/^_+/, "", n)
    # Itanium mangling: _ZN<len><ns>... -- take the first namespace component.
    if (match(n, /^ZN[0-9]+/)) {
        rest=substr(n, RSTART+2)
        len=rest+0
        sub(/^[0-9]+/, "", rest)
        key=substr(rest, 1, len)
    } else if (match(n, /^Z[0-9]+/)) {
        rest=substr(n, RSTART+1); len=rest+0
        sub(/^[0-9]+/, "", rest); key=substr(rest, 1, len)
    } else {
        key="(C and runtime)"
    }
    bytes[key]+=$1; count[key]++
} END {
    for (k in bytes) { printf "   %10d  %6d fn  %s\n", bytes[k], count[k], k }
}' /tmp/mscz-func-sizes.txt | sort -rn | head -12

echo
echo "   biggest single functions:"
head -10 /tmp/mscz-func-sizes.txt | while read -r size name; do
    printf '   %10d  %s\n' "${size}" "$(echo "${name}" | c++filt 2>/dev/null | cut -c1-110)"
done
