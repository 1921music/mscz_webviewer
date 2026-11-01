#!/usr/bin/env bash
#
# Why does a full build take half an hour, and what actually helps?
#
#   wsl -d mscz_dev -- bash /mnt/c/.../scripts/bench-build.sh [arms...]
#
# Times a fixed subset of the build -- the muse_global module, about ninety
# translation units -- from clean, once per arm, serially, so the arms are
# comparable. The whole build is the same work times ten, so a ratio here is a
# ratio there.
#
# Arms:
#   baseline    source on /mnt/c at -j nproc, which is what the scripts do
#   wslsrc      the same, with the source tree copied to the Linux filesystem
#   halfjobs    baseline at half the jobs, in case 22 of them on 15GB thrashes
#   unity       baseline plus MSCZ_USE_UNITY, so each module is a few large
#               translation units instead of ninety small ones
#   ccache      baseline through ccache, measured on the second run
#
# Run with no arguments for all of them.

set -uo pipefail

for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

WIN_SRC="${WIN_SRC:-/mnt/c/work/20260809/mscz_test}"
WSL_SRC="${WSL_SRC:-$HOME/src/mscz_test}"
BENCH="${BENCH:-$HOME/build/bench}"
TARGET="${TARGET:-muse_global}"
ARMS=("$@")
[ ${#ARMS[@]} -eq 0 ] && ARMS=(baseline wslsrc halfjobs unity ccache)

mkdir -p "${BENCH}"

# One line per arm, so the table can be printed at the end.
RESULTS="${BENCH}/results.txt"
: >"${RESULTS}"

configure() {
    local dir="$1" src="$2"
    shift 2
    rm -rf "${dir}"
    cmake -S "${src}" -B "${dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
        -DMUSE_QT_SUPPORT=OFF -DMSCZ_VENDORED_THIRDPARTY=ON \
        "$@" >"${dir}.configure.log" 2>&1
    if [ $? != 0 ]; then
        echo "   configure failed; see ${dir}.configure.log" >&2
        return 1
    fi
}

time_build() {
    local label="$1" dir="$2" jobs="$3"
    local start end
    start=$(date +%s)
    cmake --build "${dir}" -j "${jobs}" --target "${TARGET}" >"${dir}.log" 2>&1
    local rc=$?
    end=$(date +%s)
    if [ "${rc}" != 0 ]; then
        printf '%-10s FAILED (see %s.log)\n' "${label}" "${dir}" | tee -a "${RESULTS}"
        return 1
    fi
    printf '%-10s %4d s\n' "${label}" "$((end - start))" | tee -a "${RESULTS}"
}

for arm in "${ARMS[@]}"; do
    echo "== ${arm}"
    case "${arm}" in
    baseline)
        configure "${BENCH}/baseline" "${WIN_SRC}"
        time_build baseline "${BENCH}/baseline" "$(nproc)"
        ;;
    halfjobs)
        configure "${BENCH}/halfjobs" "${WIN_SRC}"
        time_build "halfjobs" "${BENCH}/halfjobs" "$(( $(nproc) / 2 ))"
        ;;
    wslsrc)
        # Copy rather than bind-mount, because the question is whether the
        # 9p bridge is what costs; a copy answers it without needing root.
        mkdir -p "$(dirname "${WSL_SRC}")"
        rsync -a --delete --exclude='build/' --exclude='.git/' "${WIN_SRC}/" "${WSL_SRC}/"
        configure "${BENCH}/wslsrc" "${WSL_SRC}"
        time_build wslsrc "${BENCH}/wslsrc" "$(nproc)"
        ;;
    unity)
        configure "${BENCH}/unity" "${WIN_SRC}" -DMSCZ_USE_UNITY=ON
        time_build unity "${BENCH}/unity" "$(nproc)"
        ;;
    wasm-baseline)
        # The thing we actually wait for: the whole wasm build, from clean,
        # exactly as scripts/build-wasm.sh configures it.
        TARGET=mscz_wasm
        rm -rf "${BENCH}/wasm-baseline"
        emcmake cmake -S "${WIN_SRC}" -B "${BENCH}/wasm-baseline" -G Ninja \
            -DCMAKE_BUILD_TYPE=MinSizeRel -DMUSE_QT_SUPPORT=OFF \
            >"${BENCH}/wasm-baseline.configure.log" 2>&1
        time_build wasm-base "${BENCH}/wasm-baseline" "$(nproc)"
        ;;
    wasm-fast)
        # The same, with the two levers the muse_global arms picked out.
        TARGET=mscz_wasm
        rm -rf "${BENCH}/wasm-fast"
        ccache --zero-stats >/dev/null 2>&1
        emcmake cmake -S "${WIN_SRC}" -B "${BENCH}/wasm-fast" -G Ninja \
            -DCMAKE_BUILD_TYPE=MinSizeRel -DMUSE_QT_SUPPORT=OFF \
            -DMSCZ_USE_UNITY=ON \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            >"${BENCH}/wasm-fast.configure.log" 2>&1
        time_build wasm-fast "${BENCH}/wasm-fast" "$(nproc)"
        printf '   wasm size: %s bytes\n' \
            "$(wc -c <"${BENCH}/wasm-fast/web/mscz-engine.wasm" 2>/dev/null || echo '?')" \
            | tee -a "${RESULTS}"
        ;;
    ccache)
        if ! command -v ccache >/dev/null; then
            echo "   ccache not installed; skipping" | tee -a "${RESULTS}"
            continue
        fi
        configure "${BENCH}/ccache" "${WIN_SRC}" \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
        ccache --zero-stats >/dev/null
        time_build "ccache-1st" "${BENCH}/ccache" "$(nproc)"
        # Second run from clean objects but a warm cache, which is the case
        # that matters when flipping between configurations.
        find "${BENCH}/ccache" -name '*.o' -delete
        time_build "ccache-2nd" "${BENCH}/ccache" "$(nproc)"
        ;;
    *)
        echo "   unknown arm" >&2
        ;;
    esac
done

echo
echo "== ${TARGET}, from clean"
cat "${RESULTS}"
