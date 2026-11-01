#!/usr/bin/env bash
#
# Runs the payload in a real browser. Phase 3's headline is "a .mscz in a
# browser", and until this existed that had only ever been shown under node.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/check-wasm-chrome.sh
#
# There is no Chromium to be had inside this distro -- Ubuntu 26.04 ships only
# a snap transitional package, which needs snapd -- and playwright's own
# download has no browser here either. But Windows has Chrome and Edge, and
# WSL can start a Windows process, so this drives one of those through the
# interop boundary. Nothing is needed on the Windows side but the browser:
# --dump-dom and --screenshot are Chrome's own flags, so there is no CDP
# client and no node on that side.
#
# Windows reaches a server bound inside WSL over localhost, which is what
# makes serving from here and loading from there work.

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASM_DIR="${WASM_DIR:-$HOME/build/mscz-wasm}"
WEB="${WASM_DIR}/web"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
SCORE="${SCORE:-${ARIA_DIR}/demos/Amazing_grace.mscz}"
PORT="${PORT:-8123}"
OUT="${REPO_DIR}/build/out/wasm"

# Starting a Windows process needs the PE handler registered with binfmt_misc.
# /etc/wsl.conf asks for interop and it is usually there, but with systemd
# running the registration sometimes is not; this puts it back. Nothing else in
# this project uses interop, which is why it lives here rather than in
# provision.sh.
if [ ! -f /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    echo ':WSLInterop:M::MZ::/init:PF' | sudo tee /proc/sys/fs/binfmt_misc/register >/dev/null \
        || { echo "could not register the Windows binary handler" >&2; exit 1; }
fi

for candidate in \
    "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" \
    "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"; do
    [ -x "${candidate}" ] && BROWSER="${candidate}" && break
done
[ -n "${BROWSER:-}" ] || { echo "no Chrome or Edge on the Windows side" >&2; exit 1; }
echo "== browser: $(basename "${BROWSER}")"

[ -f "${WEB}/mscz-engine.wasm" ] || { echo "not built: ${WEB}" >&2; exit 1; }
[ -f "${SCORE}" ] || { echo "no score at ${SCORE}" >&2; exit 1; }

# The score has to be fetchable from the page's own origin.
name="$(basename "${SCORE}")"
cp -f "${SCORE}" "${WEB}/${name}"
mkdir -p "${OUT}"

python3 -m http.server "${PORT}" --directory "${WEB}" >/dev/null 2>&1 &
server=$!
trap 'kill ${server} 2>/dev/null; rm -f "${WEB}/${name}"' EXIT

for _ in $(seq 1 50); do
    curl -fsS "http://localhost:${PORT}/index.json" >/dev/null 2>&1 && break
    sleep 0.2
done

url="http://localhost:${PORT}/index.html?score=$(python3 -c '
import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "${name}")"
echo "== loading ${url}"

# A Windows process cannot write into a WSL path it does not understand, so the
# screenshot goes to the Windows temp directory and is copied back.
win_shot='C:\Windows\Temp\mscz-page.png'
wsl_shot='/mnt/c/Windows/Temp/mscz-page.png'
rm -f "${wsl_shot}"

dom="${OUT}/browser-dom.html"
"${BROWSER}" --headless=new --disable-gpu --no-sandbox \
    --virtual-time-budget=60000 \
    --window-size=1000,1400 \
    --screenshot="${win_shot}" \
    --dump-dom "${url}" >"${dom}" 2>"${OUT}/browser-stderr.txt"

if [ ! -s "${dom}" ]; then
    echo "the browser produced no DOM; stderr:" >&2
    tail -5 "${OUT}/browser-stderr.txt" >&2
    exit 1
fi

status=$(python3 - "${dom}" <<'PY'
import re, sys
html = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.search(r'id="status"[^>]*>(.*?)</span>', html, re.S)
print(m.group(1).strip() if m else "(no status element found)")
PY
)
svgs=$(grep -o '<svg' "${dom}" | wc -l)
texts=$(grep -o '<text' "${dom}" | wc -l)
fontface=$(grep -c '@font-face' "${dom}" || true)

# The reason symbols are drawn as paths by default is that selecting the lyrics
# should not drag the notation along with it. That is a claim about what is
# inside <text>, so check it: SMuFL code points live in the private use area,
# so if any appear in the rendered text the notation is selectable.
selection=$(python3 - "${dom}" <<'PY'
import re, sys
html = open(sys.argv[1], encoding="utf-8", errors="replace").read()
runs = re.findall(r'<text[^>]*>(.*?)</text>', html, re.S)
pua = [t for t in runs if any(0xE000 <= ord(c) <= 0xF8FF for c in t)]
words = " ".join(runs)
print(f"{len(runs)}|{len(pua)}|{'Amazing' in words or 'AMAZING' in words}")
PY
)
runs=${selection%%|*}
rest=${selection#*|}
pua=${rest%%|*}
haslyrics=${rest##*|}

echo
echo "== what the browser did"
echo "   status line: ${status}"
echo "   <svg> elements in the document: ${svgs}"
echo "   <text> elements (selectable lyrics and titles): ${texts}"
echo "   @font-face blocks (embedded subsets): ${fontface}"
echo "   text runs: ${runs}, of which notation glyphs: ${pua}, lyrics present: ${haslyrics}"

if [ -f "${wsl_shot}" ]; then
    cp -f "${wsl_shot}" "${OUT}/browser-page.png"
    echo "   screenshot: ${OUT}/browser-page.png ($(wc -c <"${OUT}/browser-page.png") bytes)"
fi

case "${status}" in
    *page*) ;;
    *) echo "the page did not report a rendered score" >&2; exit 1 ;;
esac
[ "${svgs}" -gt 0 ] || { echo "no SVG in the document" >&2; exit 1; }
[ "${pua}" = "0" ] || {
    echo "notation glyphs are in the selectable text, which the path default exists to prevent" >&2
    exit 1
}
[ "${haslyrics}" = "True" ] || { echo "no lyrics in the text runs" >&2; exit 1; }
echo
echo "   the engine ran in a browser, drew the score, and the only selectable"
echo "   text is words -- no notation."
