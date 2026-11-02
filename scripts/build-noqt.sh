#!/usr/bin/env bash
#
# Phase 2's build: the same tree with MUSE_QT_SUPPORT off.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/build-noqt.sh          # build, summarise
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/build-noqt.sh --errors [pattern]
#
# Keeps going past the first failure, so one pass shows the whole surface, and
# reports one error per failing translation unit rather than a wall of them.

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-noqt}"
LOG="${BUILD_DIR}.log"

if [ "${1:-}" = "--errors" ]; then
    want="${2:-}"
    awk -v want="$want" '
      /^FAILED: \[code=[0-9]+\]/ { f=$3; sub(/.*\/CMakeFiles\/[^\/]*\.dir\//, "", f); sub(/\.o$/, "", f); cur=f; next }
      /error:|fatal error:/ {
          if (want == "" || index(cur, want) > 0) {
              l=$0
              sub(/^.*\/(vendor|src)\//, "", l)
              print "[" cur "] " substr(l, 1, 140)
          }
      }
    ' "$LOG" | sort -u | head -40
    exit 0
fi

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=/usr/bin/gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
        -DMUSE_QT_SUPPORT=OFF >/dev/null || exit 1
fi

cmake --build "${BUILD_DIR}" -j "$(nproc)" -- -k 0 >"${LOG}" 2>&1
build_status=$?

printf 'failing translation units: %s\n' "$(grep -acE '^FAILED:' "${LOG}")"
awk '
  /^FAILED: \[code=[0-9]+\]/ { f=$3; sub(/.*\/CMakeFiles\/[^\/]*\.dir\//, "", f); sub(/\.o$/, "", f); cur=f; shown[cur]=0; next }
  /error:|fatal error:/ {
      if (cur != "" && shown[cur] == 0) {
          l=$0; sub(/^.*error: /, "", l)
          printf "  %-44s %s\n", cur, substr(l, 1, 62)
          shown[cur]=1
      }
  }
' "${LOG}" | sort -u

exit "${build_status}"
