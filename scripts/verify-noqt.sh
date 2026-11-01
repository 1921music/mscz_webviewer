#!/usr/bin/env bash
#
# Checks that the Qt-free build really is Qt-free, and that it still renders.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/verify-noqt.sh [score]
#
# "Builds without Qt headers" is not the same as "has no Qt in it", so this
# looks at the linked binary rather than the build log.

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
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-noqt}"
SCORE="${1:-${ARIA_DIR}/demos/Amazing_grace.mscz}"
OUT="${REPO_DIR}/build/noqt"
PROBE="${BUILD_DIR}/src/mscz_probe"

[ -x "${PROBE}" ] || { echo "not built: ${PROBE}" >&2; exit 1; }

echo "=== Qt symbols left in the libraries ==="
status=0
while IFS= read -r lib; do
    n=$(nm -C --undefined-only "$lib" 2>/dev/null | grep -cE '(^| )Q[A-Z][A-Za-z]*(::|\()|qt_' || true)
    printf '  %-26s %4d\n' "$(basename "$lib")" "$n"
    [ "$n" -ne 0 ] && status=1
done < <(find "${BUILD_DIR}" -name '*.a' | sort)

echo
echo "=== Qt shared libraries the binary needs ==="
if ldd "${PROBE}" 2>/dev/null | grep -i 'libqt'; then
    echo "  ^ still linked against Qt"
    status=1
else
    echo "  none"
fi

echo
echo "=== does it render? ==="
rm -rf "${OUT}"
mkdir -p "${OUT}"
if ! "${PROBE}" --svg --dpi 110 --out "${OUT}" "${SCORE}"; then
    echo "  render failed" >&2
    exit 1
fi

for f in "${OUT}"/page-*.svg; do
    [ -e "$f" ] || continue
    rsvg-convert -w 909 "$f" -o "${f%.svg}.png" 2>/dev/null || true
done
ls -la "${OUT}" | tail -6

exit "${status}"
