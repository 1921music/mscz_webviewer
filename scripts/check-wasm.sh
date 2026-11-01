#!/usr/bin/env bash
#
# Phase 3's verification: does the wasm build lay a score out exactly as the
# native Qt-free build does?
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/check-wasm.sh
#
# The comparison is against the native Qt-free build's own fingerprint, not
# against test/baselines (which came from the Qt build and still differ by the
# font-substitution gap Phase 2 ended on). What is being asked here is narrower
# and should be exact: emscripten's libc, 32-bit pointers, the trimmed FreeType
# module set and HB_TINY must not move anything.
#
# Runs under the node emsdk brings, because this distro has no Chromium for
# playwright to drive. The browser path is the same wasm with a different
# loader; web/index.html is there to exercise it by hand.

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
NATIVE_DIR="${NATIVE_DIR:-$HOME/build/mscz-vendored}"
ARIA_DIR="${ARIA_DIR:-/mnt/c/work/20260809/aria}"
OUT="${REPO_DIR}/build/out/wasm"

NODE=$(ls -d "$HOME"/.local/share/mise/installs/emsdk/*/node/*/bin/node 2>/dev/null | head -1)

[ -f "${WASM_DIR}/web/mscz-engine.wasm" ] || { echo "not built: ${WASM_DIR}/web" >&2; exit 1; }
[ -x "${NODE}" ] || { echo "no node under emsdk" >&2; exit 1; }
[ -x "${NATIVE_DIR}/src/mscz_probe" ] || {
    echo "no native reference build at ${NATIVE_DIR}; run scripts/build-noqt.sh with" >&2
    echo "BUILD_DIR=${NATIVE_DIR} and -DMSCZ_VENDORED_THIRDPARTY=ON" >&2
    exit 1
}

mkdir -p "${OUT}"
status=0

for score in "${ARIA_DIR}/demos/Amazing_grace.mscz" "${ARIA_DIR}/demos/goldberg.mscz"; do
    [ -f "${score}" ] || continue
    name="$(basename "${score%.*}")"
    reference="${OUT}/${name}.native.fingerprint"

    echo "== ${name}"
    "${NATIVE_DIR}/src/mscz_probe" --fingerprint "${score}" >"${reference}" 2>/dev/null

    "${NODE}" "${REPO_DIR}/scripts/check-wasm-node.js" \
        --web "${WASM_DIR}/web" \
        --score "${score}" \
        --baseline "${reference}" || status=1

    # And the drawing itself, byte for byte. The fingerprint is sorted, so it
    # cannot see a difference in the order elements are painted in -- which is
    # exactly what went wrong once, when an unstable std::sort in paint.cpp
    # ordered equal-z items differently under libc++ than under libstdc++.
    native_svg="${OUT}/${name}.native"
    rm -rf "${native_svg}"
    mkdir -p "${native_svg}"
    "${NATIVE_DIR}/src/mscz_probe" --svg --dpi 300 --page 1 --out "${native_svg}" "${score}" >/dev/null 2>&1
    if [ -f "${native_svg}/page-001.svg" ]; then
        if cmp -s "${native_svg}/page-001.svg" "${OUT}/${name}-page-001.svg"; then
            echo "page 1 SVG: identical to the native build"
        else
            echo "page 1 SVG: DIFFERS from the native build"
            cmp "${native_svg}/page-001.svg" "${OUT}/${name}-page-001.svg" | head -1
            status=1
        fi
    fi
    echo
done

exit "${status}"
