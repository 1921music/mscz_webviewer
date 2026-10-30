#!/usr/bin/env bash
set -euo pipefail
echo "=== system shared libraries (stripped) ==="
for n in freetype harfbuzz z; do
    f=$(ls -S /usr/lib/x86_64-linux-gnu/lib${n}.so.* 2>/dev/null | head -1 || true)
    [ -n "${f:-}" ] || continue
    printf '%-50s %8d KB\n' "$(basename "$f")" "$(( $(wc -c <"$f") / 1024 ))"
done
echo
echo "=== vendored freetype source (not vendored here; aria's copy) ==="
d=/mnt/c/work/20260809/aria/src/framework/draw/thirdparty/freetype/freetype-2.14.1
if [ -d "$d" ]; then
    printf 'whole tree      %8d KB\n' "$(du -sk "$d" | cut -f1)"
    printf 'src/ only       %8d KB  (%d .c files)\n' \
        "$(du -sk "$d/src" | cut -f1)" "$(find "$d/src" -name '*.c' | wc -l)"
fi
