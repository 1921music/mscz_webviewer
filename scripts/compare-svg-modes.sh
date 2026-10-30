#!/usr/bin/env bash
#
# Renders a score in all four combinations of symbol and font handling, and
# reports size and how each survives with no fonts installed.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/compare-svg-modes.sh [score]
#
# The "no fonts" rasterisation points fontconfig at an empty directory, so only
# what the document carries itself comes out.

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
mkdir -p "${OUT}"
export QT_QPA_PLATFORM=offscreen

EMPTY_FONTS=$(mktemp -d)
trap 'rm -rf "${EMPTY_FONTS}"' EXIT
mkdir -p "${EMPTY_FONTS}/cache"
cat >"${EMPTY_FONTS}/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig><dir>${EMPTY_FONTS}</dir><cachedir>${EMPTY_FONTS}/cache</cachedir></fontconfig>
EOF

printf '%-18s %9s %9s %9s %9s\n' mode raw gzip brotli 'no-font px'
for symbols in font path; do
    for fonts in reference embed; do
        name="${symbols}-${fonts}"
        dir="${OUT}/${name}"
        mkdir -p "${dir}"

        args=(--svg --symbols "${symbols}" --dpi "${DPI}" --out "${dir}")
        [ "${fonts}" = embed ] && args+=(--embed-fonts)
        "${PROBE}" "${args[@]}" "${SCORE}" >/dev/null 2>&1

        f="${dir}/page-001.svg"
        raw=$(wc -c <"$f")
        gz=$(gzip -9 -c "$f" | wc -c)
        br=$(brotli -q 11 -c "$f" | wc -c)

        FONTCONFIG_FILE="${EMPTY_FONTS}/fonts.conf" \
            rsvg-convert -w 909 "$f" -o "${OUT}/${name}-nofonts.png" 2>/dev/null || true
        rsvg-convert -w 909 "$f" -o "${OUT}/${name}-withfonts.png" 2>/dev/null || true

        printf '%-18s %9d %9d %9d %9d\n' "${name}" "$raw" "$gz" "$br" \
            "$(wc -c <"${OUT}/${name}-nofonts.png" 2>/dev/null || echo 0)"
    done
done

echo
echo "embedded faces and their subset sizes (font-reference has none):"
for name in font-embed path-embed; do
    f="${OUT}/${name}/page-001.svg"
    [ -f "$f" ] || continue
    n=$(grep -o '@font-face' "$f" | wc -l)
    b64=$(grep -o 'base64,[A-Za-z0-9+/=]*' "$f" | awk '{ s += length($0) - 7 } END { print s+0 }')
    printf '  %-12s %d face(s), %d bytes of base64 (~%d bytes of font)\n' \
        "$name" "$n" "$b64" "$((b64 * 3 / 4))"
done

echo
echo "PNGs in ${OUT}: <mode>-{withfonts,nofonts}.png"
