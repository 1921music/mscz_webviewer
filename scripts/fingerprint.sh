#!/usr/bin/env bash
#
# Writes a score's layout fingerprint to build/out/, and diffs it against a
# saved baseline if there is one.
#
#   # record a baseline before a change that could move layout
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/fingerprint.sh --save
#
#   # after the change, see what moved
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/fingerprint.sh
#
# Baselines go in test/baselines/ and are committed; they are the regression
# check for Phase 2, where the font provider swaps from Qt to FreeType and
# every symbol bounding box can shift.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-host}"
BASELINE_DIR="${REPO_DIR}/test/baselines"
OUT_DIR="${REPO_DIR}/build/out"

SAVE=0
SCORES=()
for arg in "$@"; do
    case "${arg}" in
        --save) SAVE=1 ;;
        -*) echo "unknown option: ${arg}" >&2; exit 2 ;;
        *) SCORES+=("${arg}") ;;
    esac
done

if [ ${#SCORES[@]} -eq 0 ]; then
    SCORES=(
        "${ARIA_DIR}/demos/Amazing_grace.mscz"
        "${ARIA_DIR}/demos/goldberg.mscz"
    )
fi

PROBE="${BUILD_DIR}/src/mscz_probe"
if [ ! -x "${PROBE}" ]; then
    echo "mscz_probe not built. Run scripts/build-host.sh first." >&2
    exit 1
fi

export QT_QPA_PLATFORM=offscreen
mkdir -p "${OUT_DIR}"
[ "${SAVE}" -eq 1 ] && mkdir -p "${BASELINE_DIR}"

status=0
for score in "${SCORES[@]}"; do
    if [ ! -f "${score}" ]; then
        echo "skip (not found): ${score}"
        continue
    fi

    name="$(basename "${score%.*}")"
    current="${OUT_DIR}/${name}.fingerprint"
    baseline="${BASELINE_DIR}/${name}.fingerprint"

    "${PROBE}" --fingerprint "${score}" >"${current}" 2>/dev/null

    if [ "${SAVE}" -eq 1 ]; then
        cp "${current}" "${baseline}"
        echo "saved  ${name}  ($(wc -l <"${current}") elements)"
        continue
    fi

    if [ ! -f "${baseline}" ]; then
        echo "no baseline for ${name}; run with --save"
        continue
    fi

    if diff -q "${baseline}" "${current}" >/dev/null; then
        echo "same   ${name}  ($(wc -l <"${current}") elements)"
    else
        moved=$(diff "${baseline}" "${current}" | grep -c '^[<>]' || true)
        echo "MOVED  ${name}  (${moved} differing lines)"
        diff "${baseline}" "${current}" | head -20
        status=1
    fi
done

exit "${status}"
