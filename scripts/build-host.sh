#!/usr/bin/env bash
#
# Configures and builds aria's render core on the Linux host (Qt6 from apt).
# This is Phase 1: a Qt-backed build whose PNG/PDF output is the reference the
# SVG provider gets compared against.
#
# Run inside the mscz_dev distro:
#   wsl -d mscz_dev -- bash /mnt/c/work/20260809/mscz_test/scripts/build-host.sh
#
# Override paths with ARIA_DIR / BUILD_DIR, and targets with the arguments.

set -euo pipefail

# Work whether or not this was started from a login shell: cmake and ninja come
# from mise, whose shims /etc/profile.d puts on PATH only for login shells.
for extra in "$HOME/.local/bin" "$HOME/.local/share/mise/shims"; do
    case ":$PATH:" in
        *":$extra:"*) ;;
        *) [ -d "$extra" ] && PATH="$extra:$PATH" ;;
    esac
done
export PATH

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/mscz-host}"
JOBS="${JOBS:-$(nproc)}"
TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
    TARGETS=(mscz_probe)
fi

if [ ! -d "${REPO_DIR}/vendor/aria/src/engraving" ]; then
    echo "vendor/aria is missing. Run scripts/vendor-aria.sh first." >&2
    exit 1
fi

# MUSE_APP_INSTALL_PREFIX / MUSE_APP_INSTALL_NAME feed muse_framework_config.h
# via #cmakedefine. aria's sandbox build only sets them for Android, and the
# Linux branch of GlobalConfiguration::resolveAppDataPath() needs them, so a
# Linux host build fails to compile without these. The render core embeds its
# resources, so the path they produce is never actually used.
cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++

cmake --build "${BUILD_DIR}" --target "${TARGETS[@]}" -j "${JOBS}"

echo
echo "Built into ${BUILD_DIR}:"
for target in "${TARGETS[@]}"; do
    find "${BUILD_DIR}" -maxdepth 2 -name "${target}" -type f -printf '  %p\n' 2>/dev/null || true
done
