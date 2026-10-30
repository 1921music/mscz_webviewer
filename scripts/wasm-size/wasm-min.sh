#!/usr/bin/env bash
# Same as wasm.sh, but with FreeType cut down to the modules this project
# needs, to see how much of its size is font formats we never open.
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
BUILD="$HOME/build/ft-wasm-min"
PREFIX="$HOME/build/ft-wasm-min-install"

echo "== configuring, modules trimmed via FT_CONFIG_MODULES_H"
emcmake cmake -S "${FT_SRC}" -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_ZLIB=ON \
    -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON \
    -DFT_DISABLE_BROTLI=ON \
    -DCMAKE_C_FLAGS="-Oz -DFT_CONFIG_MODULES_H='<${HERE}/ftmodule-min.h>'" \
    >/dev/null 2>&1

cmake --build "${BUILD}" -j "$(nproc)" >/dev/null 2>&1
cmake --install "${BUILD}" >/dev/null 2>&1

LIB=$(find "${PREFIX}" -name 'libfreetype.a' | head -1)
printf '   libfreetype.a  %8d KB (archive)\n' "$(( $(wc -c <"${LIB}") / 1024 ))"

cd "${HERE}"
emcc -Oz -flto probe.c -o probe-min.js \
    -I"${PREFIX}/include/freetype2" "${LIB}" \
    -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 \
    >/dev/null 2>&1

echo
printf '   %-18s %8s %10s\n' file raw brotli
for f in empty.wasm probe.wasm probe-min.wasm; do
    [ -f "$f" ] || continue
    printf '   %-18s %8d %10d\n' "$f" "$(wc -c <"$f")" "$(brotli -q 11 -c "$f" | wc -c)"
done

echo
echo "== still works?"
node probe-min.js ../../vendor/aria/fonts/leland/Leland.otf E050 | sed 's/^/   /' | head -4
echo "== and a TrueType face?"
node probe-min.js ../../vendor/aria/fonts/mscoreTab.ttf 0041 2>&1 | sed 's/^/   /' | head -3
