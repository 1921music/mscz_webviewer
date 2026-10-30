#!/usr/bin/env bash
# What does HarfBuzz cost in a wasm build, on top of FreeType?
#
# Compiles the single-file amalgam (src/harfbuzz.cc) rather than the 142
# separate sources, with HB_TINY -- which keeps the OpenType shaper, the part
# MuseScore actually uses, and drops the rest.
#
# Nothing is run from inside the unpacked tree; paths are passed in.
set -euo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HB="$HOME/stage/harfbuzz"
FT_PREFIX="$HOME/build/ft-wasm-min-install"
WORK="$HOME/build/hb-wasm"

if [ ! -f "${HB}/src/harfbuzz.cc" ]; then
    echo "HarfBuzz not unpacked at ${HB}; run inspect.sh first" >&2
    exit 1
fi
if [ ! -d "${FT_PREFIX}/include/freetype2" ]; then
    echo "trimmed FreeType not built; run ../ftsize/wasm-min.sh first" >&2
    exit 1
fi

mkdir -p "${WORK}"

echo "== compiling the HarfBuzz amalgam with HB_TINY"
emcc -Oz -flto -std=c++17 -c "${HB}/src/harfbuzz.cc" -o "${WORK}/harfbuzz.o" \
    -DHB_TINY \
    -DHAVE_FREETYPE=1 \
    -DHB_NO_MT \
    -I"${HB}/src" \
    -I"${FT_PREFIX}/include/freetype2" \
    -Wno-unused-command-line-argument \
    2>&1 | grep -viE "^(cache|ports|emcc):INFO" | head -15 || true

printf '   harfbuzz.o  %8d KB (object, before dead-code elimination)\n' \
    "$(( $(wc -c <"${WORK}/harfbuzz.o") / 1024 ))"

echo
echo "== linking a probe that shapes a string and reads the glyph outlines"
cat >"${WORK}/shape.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ft.h>

int main(int argc, char** argv)
{
    if (argc < 3) { return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { return 1; }
    fclose(f);

    FT_Library lib; FT_Face face;
    if (FT_Init_FreeType(&lib)) { return 1; }
    if (FT_New_Memory_Face(lib, data, (FT_Long)size, 0, &face)) { return 1; }
    FT_Set_Char_Size(face, 1000 << 6, 1000 << 6, 0, 0);

    hb_font_t* font = hb_ft_font_create(face, NULL);
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, argv[2], -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, NULL, 0);

    unsigned int n = 0;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    printf("shaped %u glyphs:", n);
    for (unsigned int i = 0; i < n; ++i) {
        printf(" %u(+%d)", info[i].codepoint, pos[i].x_advance);
    }
    printf("\n");

    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    free(data);
    return 0;
}
EOF

emcc -Oz -flto "${WORK}/shape.c" -o "${HERE}/shape.js" \
    "${WORK}/harfbuzz.o" \
    -I"${HB}/src" -I"${FT_PREFIX}/include/freetype2" \
    "$(find "${FT_PREFIX}" -name libfreetype.a | head -1)" \
    -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 \
    2>&1 | grep -viE "^(cache|ports|emcc):INFO" | head -10 || true

echo
echo "== sizes"
printf '   %-22s %9s %9s\n' '' raw brotli
for f in "${HERE}/../ftsize/nolib.wasm" "${HERE}/../ftsize/probe-min.wasm" "${HERE}/shape.wasm"; do
    [ -f "$f" ] || continue
    printf '   %-22s %9d %9d\n' "$(basename "$f")" "$(wc -c <"$f")" "$(brotli -q 11 -c "$f" | wc -c)"
done

echo
echo "== does it shape?"
node "${HERE}/shape.js" /mnt/c/work/20260809/mscz_test/vendor/aria/fonts/edwin/Edwin-Roman.otf "Amazing" | sed 's/^/   /'
