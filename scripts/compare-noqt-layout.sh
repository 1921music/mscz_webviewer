#!/usr/bin/env bash
#
# How much did the layout move when the font provider went from Qt to FreeType?
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/compare-noqt-layout.sh
#
# Compares the Qt-free build's fingerprint against the committed baselines,
# which were recorded from the Qt build, and summarises by element type so the
# answer is "what moved" rather than "something moved".

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
PROBE="${BUILD_DIR:-$HOME/build/mscz-noqt}/src/mscz_probe"
OUT="${REPO_DIR}/build/out"

[ -x "${PROBE}" ] || { echo "not built: ${PROBE}" >&2; exit 1; }

export QT_QPA_PLATFORM=offscreen
mkdir -p "${OUT}"

for score in "${ARIA_DIR}/demos/Amazing_grace.mscz" "${ARIA_DIR}/demos/goldberg.mscz"; do
    [ -f "${score}" ] || continue
    name="$(basename "${score%.*}")"
    baseline="${REPO_DIR}/test/baselines/${name}.fingerprint"
    [ -f "${baseline}" ] || continue

    current="${OUT}/${name}.noqt.fingerprint"
    "${PROBE}" --fingerprint "${score}" >"${current}" 2>/dev/null

    total=$(wc -l <"${baseline}")
    now=$(wc -l <"${current}")
    moved=$(diff "${baseline}" "${current}" | grep -cE '^[<>]' || true)

    printf '%-16s baseline %6d  now %6d  differing lines %6d  (%d%% of elements)\n' \
        "${name}" "${total}" "${now}" "${moved}" "$((moved * 50 / (total == 0 ? 1 : total)))"

    echo "  by element type, the ones that moved most:"
    diff "${baseline}" "${current}" \
        | grep -E '^[<>]' | awk '{print $3}' | sort | uniq -c | sort -rn | head -8 \
        | sed 's/^/    /'
    echo
done
