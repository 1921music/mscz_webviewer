#!/usr/bin/env bash
#
# Phase 1: render a score through aria's Qt-backed render core, to get the
# reference output the SVG provider will be compared against.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/render-ref.sh [score] [outdir]
#
# Defaults to a demo score from the aria checkout.

set -euo pipefail

ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
BUILD_DIR="${BUILD_DIR:-$HOME/build/aria-host}"
SCORE="${1:-${ARIA_DIR}/demos/Amazing_grace.mscz}"
OUT_DIR="${2:-$HOME/out/ref/$(basename "${SCORE%.*}")}"
DPI="${DPI:-144}"

PROBE="${BUILD_DIR}/aria-engraving/score_render_probe"
if [ ! -x "${PROBE}" ]; then
    echo "score_render_probe not built. Run scripts/build-host.sh first." >&2
    exit 1
fi

if [ ! -f "${SCORE}" ]; then
    echo "Score not found: ${SCORE}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

# The render core builds against Qt Gui; offscreen keeps it from needing a display.
export QT_QPA_PLATFORM=offscreen

echo "Rendering ${SCORE} at ${DPI} dpi into ${OUT_DIR}"
"${PROBE}" --dpi "${DPI}" --output-dir "${OUT_DIR}" "${SCORE}"

echo
echo "Output:"
ls -la "${OUT_DIR}"
