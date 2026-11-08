#!/usr/bin/env bash
#
# Phase 3's build: the Qt-free tree through emscripten.
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/build-wasm.sh
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/build-wasm.sh --quick
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/build-wasm.sh --errors [pattern]
#
# --quick builds into its own directory without -Oz or LTO. Use it for
# anything that asks whether the engine is right rather than how big it is:
# 98% of a build is compiling, and compiling for size is what costs.
# Size figures must come from the default build.
#
# Keeps going past the first failure so one pass shows the whole surface, and
# reports one error per failing translation unit rather than a wall of them --
# the same shape as build-noqt.sh, since the exercise is the same.
#
# The payload lands in <build>/web, ready to serve:
#
#   python3 -m http.server --directory ~/build/mscz-wasm/web

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_TYPE=MinSizeRel
SUFFIX=""
if [ "${1:-}" = "--quick" ]; then
    BUILD_TYPE=Debug
    SUFFIX="-quick"
    shift
fi

BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-webviewer${SUFFIX}}"
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

{
    # emcmake puts the emscripten toolchain file in place; everything else is
    # the same configuration Phase 2 uses.
    mise exec -- emcmake cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DMUSE_QT_SUPPORT=OFF \
        -DMSCZ_PLAYBACK="${MSCZ_PLAYBACK:-EAS}" \
        ${MSCZ_CMAKE_EXTRA:-} \
        || exit 1
}

mise exec -- cmake --build "${BUILD_DIR}" -j "${BUILD_JOBS:-$(nproc)}" -- -k 0 >"${LOG}" 2>&1
build_status=$?

failed=$(grep -acE '^FAILED:' "${LOG}")
printf 'failing translation units: %s\n' "${failed}"
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

if [ "${build_status}" != "0" ]; then
    tail -30 "${LOG}"
    exit "${build_status}"
fi

echo
echo "== payload in ${BUILD_DIR}/web"
printf '   %-22s %9s %9s\n' '' raw brotli
for f in "${BUILD_DIR}"/web/mscz-engine.wasm "${BUILD_DIR}"/web/mscz-engine.js; do
    [ -f "$f" ] || continue
    printf '   %-22s %9d %9d\n' "$(basename "$f")" \
        "$(wc -c <"$f")" "$(brotli -q 11 -c "$f" | wc -c)"
done
