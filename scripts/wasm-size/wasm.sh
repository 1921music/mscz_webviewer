#!/usr/bin/env bash
# Builds FreeType for wasm from the copy aria bundles, with every optional
# dependency and every font format we do not need switched off, then links the
# outline probe against it. The point is a real number for what FreeType costs.
#
# Does not use emscripten ports: an intercepting proxy can truncate a GitHub
# tarball, so ports cannot be fetched reliably.
set -euo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FT_SRC=/mnt/c/work/20260809/aria/src/framework/draw/thirdparty/freetype/freetype-2.14.1
BUILD="$HOME/build/ft-wasm"
PREFIX="$HOME/build/ft-wasm-install"

if [ ! -d "${FT_SRC}" ]; then
    echo "FreeType source not found at ${FT_SRC}" >&2
    exit 1
fi

echo "== configuring FreeType for wasm"
emcmake cmake -S "${FT_SRC}" -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_ZLIB=ON \
    -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON \
    -DFT_DISABLE_BROTLI=ON \
    >/dev/null 2>&1

echo "== building"
cmake --build "${BUILD}" -j "$(nproc)" >/dev/null 2>&1
cmake --install "${BUILD}" >/dev/null 2>&1

LIB=$(find "${PREFIX}" -name 'libfreetype.a' | head -1)
printf '   libfreetype.a  %8d KB (archive, before dead-code elimination)\n' "$(( $(wc -c <"${LIB}") / 1024 ))"

echo
echo "== linking the outline probe"
cd "${HERE}"
emcc -Oz -flto probe.c -o probe.js \
    -I"${PREFIX}/include/freetype2" "${LIB}" \
    -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 \
    >/dev/null 2>&1

echo "== sizes"
printf '   %-14s %8s %10s\n' file raw brotli
for f in empty.wasm probe.wasm probe.js; do
    [ -f "$f" ] || continue
    printf '   %-14s %8d %10d\n' "$f" "$(wc -c <"$f")" "$(brotli -q 11 -c "$f" | wc -c)"
done

echo
echo "== does it run?"
node probe.js ../../vendor/aria/fonts/leland/Leland.otf E050 | sed 's/^/   /'
