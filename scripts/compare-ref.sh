#!/usr/bin/env bash
#
# Renders a score with mscz_probe and, if aria's Phase 1 reference PDFs are
# around, rasterises those too, so the two can be put side by side.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/compare-ref.sh [score]
#
# Output lands in build/out/ (gitignored) as mine-pageN.png and ref-pageN.png.

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
REF_PDF_DIR="${REF_PDF_DIR:-$HOME/out/ref/$(basename "${SCORE%.*}")}"

PROBE="${BUILD_DIR}/src/mscz_probe"
if [ ! -x "${PROBE}" ]; then
    echo "mscz_probe not built. Run scripts/build-host.sh first." >&2
    exit 1
fi

rm -rf "${OUT}"
mkdir -p "${OUT}"

# No display available; the Phase 1 build still links Qt Gui.
export QT_QPA_PLATFORM=offscreen

echo "== ours (vendored tree, mscz_probe)"
"${PROBE}" --dpi "${DPI}" --out "${OUT}" "${SCORE}"
for f in "${OUT}"/page-*.png; do
    [ -e "$f" ] || continue
    mv "$f" "${OUT}/mine-$(basename "$f" | sed 's/^page-0*//')"
done

if [ -d "${REF_PDF_DIR}" ]; then
    echo
    echo "== reference (aria's own probe, from ${REF_PDF_DIR})"
    for pdf in "${REF_PDF_DIR}"/page-*.pdf; do
        [ -e "${pdf}" ] || continue
        n=$(basename "${pdf}" .pdf | sed 's/^page-0*//')
        pdftoppm -r "${DPI}" -png -f 1 -l 1 "${pdf}" "${OUT}/ref-${n}"
        mv "${OUT}/ref-${n}-1.png" "${OUT}/ref-${n}.png"
    done
else
    echo
    echo "No reference PDFs at ${REF_PDF_DIR}; skipping."
fi

echo
ls -la "${OUT}"
