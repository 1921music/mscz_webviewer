#!/usr/bin/env bash
# probe.wasm includes stdio, malloc and double formatting from libc, which are
# not free. Build the same program with the FreeType calls removed, so the
# difference is FreeType's own contribution.
set -euo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cat >nolib.c <<'EOF'
/* Same file reading and formatted output as probe.c, no FreeType. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_path[4096];

int main(int argc, char** argv)
{
    if (argc < 3) {
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        return 1;
    }
    fclose(f);

    unsigned long code = strtoul(argv[2], NULL, 16);
    printf("family=%s style=%s glyphs=%ld upem=%d\n", "x", "y", (long)size, 1000);
    printf("U+%04lX -> glyph %u\n", code, (unsigned)data[0]);
    size_t n = snprintf(g_path, sizeof(g_path), "M%.1f %.1f C%.1f %.1f %.1f %.1f ",
                        1.5, 2.5, 3.5, 4.5, 5.5, 6.5);
    printf("advance=%ld contours=%d points=%d\n", (long)n, 4, 136);
    printf("path (%zu chars): %.200s\n", n, g_path);
    free(data);
    return 0;
}
EOF

emcc -Oz -flto nolib.c -o nolib.js -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 >/dev/null 2>&1

base_raw=$(wc -c <nolib.wasm)
base_br=$(brotli -q 11 -c nolib.wasm | wc -c)

printf '%-26s %9s %9s\n' '' raw brotli
printf '%-26s %9d %9d\n' 'libc baseline (no FT)' "$base_raw" "$base_br"
for f in probe-min.wasm probe.wasm; do
    [ -f "$f" ] || continue
    raw=$(wc -c <"$f")
    br=$(brotli -q 11 -c "$f" | wc -c)
    printf '%-26s %9d %9d\n' "$f" "$raw" "$br"
    printf '%-26s %9d %9d\n' "  -> FreeType's share" "$((raw - base_raw))" "$((br - base_br))"
done
