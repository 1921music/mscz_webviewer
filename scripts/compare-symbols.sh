#!/usr/bin/env bash
#
# Renders a score both ways -- symbols as <text> in the music font, and symbols
# as glyph paths in <defs> -- and reports size and whether each rasterises
# without the fonts installed.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/compare-symbols.sh [score]
#
# The "no fonts" column is the point of path mode: rsvg-convert is told to look
# only at a directory with no fonts in it, so anything that needs a font to
# render disappears.

set -euo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-host}"
SCORE="${1:-${ARIA_DIR}/demos/Amazing_grace.mscz}"
DPI="${DPI:-110}"
OUT="${REPO_DIR}/build/out"
PROBE="${BUILD_DIR}/src/mscz_probe"

[ -x "${PROBE}" ] || { echo "mscz_probe not built" >&2; exit 1; }

rm -rf "${OUT}"
mkdir -p "${OUT}/font" "${OUT}/path"
export QT_QPA_PLATFORM=offscreen

for mode in font path; do
    "${PROBE}" --svg --symbols "${mode}" --dpi "${DPI}" --out "${OUT}/${mode}" "${SCORE}" >/dev/null
done

# An empty font directory, so fontconfig finds nothing and only self-contained
# markup survives.
EMPTY_FONTS=$(mktemp -d)
trap 'rm -rf "${EMPTY_FONTS}"' EXIT
cat >"${EMPTY_FONTS}/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig><dir>${EMPTY_FONTS}</dir><cachedir>${EMPTY_FONTS}/cache</cachedir></fontconfig>
EOF

printf '%-6s %10s %10s %10s   %s\n' mode raw gzip brotli 'rasterised'
for mode in font path; do
    f="${OUT}/${mode}/page-001.svg"
    raw=$(wc -c <"$f")
    gz=$(gzip -9 -c "$f" | wc -c)
    br=$(brotli -q 11 -c "$f" | wc -c)

    rsvg-convert -w 909 "$f" -o "${OUT}/${mode}-withfonts.png" 2>/dev/null
    FONTCONFIG_FILE="${EMPTY_FONTS}/fonts.conf" \
        rsvg-convert -w 909 "$f" -o "${OUT}/${mode}-nofonts.png" 2>/dev/null

    printf '%-6s %10d %10d %10d   %s / %s\n' "$mode" "$raw" "$gz" "$br" \
        "$(wc -c <"${OUT}/${mode}-withfonts.png") B with fonts" \
        "$(wc -c <"${OUT}/${mode}-nofonts.png") B without"
done

echo
echo "defs in path mode:"
grep -c '<path id=' "${OUT}/path/page-001.svg" || true
echo "uses in path mode:"
grep -o '<use ' "${OUT}/path/page-001.svg" | wc -l
echo
echo "PNGs in ${OUT}: {font,path}-{withfonts,nofonts}.png"
