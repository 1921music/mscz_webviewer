#!/usr/bin/env bash
#
# Provisions the mscz_dev WSL distribution for building the MuseScore-derived
# render core (aria) natively with Qt6, and later for WebAssembly.
#
# Run as root inside a freshly registered Ubuntu 26.04 WSL distro:
#   wsl -d mscz_dev -u root -- bash /tmp/provision.sh dev
#
# Everything that mise can provide comes from mise. Only the C++ toolchain and
# Qt6 come from apt, because mise has no package for either.

set -euo pipefail

DEV_USER="${1:-dev}"
# Directory of PEM files exported from the Windows certificate store, or empty.
WIN_CA_DIR="${2:-}"

log() { printf '\n== %s\n' "$*"; }

if [ "$(id -u)" -ne 0 ]; then
    echo "provision.sh must run as root" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# WSL behaviour: default to the dev user, and keep the Windows PATH out of the
# build environment so a stray Windows tool can never be picked up.
# ---------------------------------------------------------------------------
log "Writing /etc/wsl.conf"
cat >/etc/wsl.conf <<EOF
[user]
default=${DEV_USER}

[interop]
enabled=true
appendWindowsPath=false
EOF

# ---------------------------------------------------------------------------
# apt: C++ toolchain, Qt6, and the native libraries muse_draw links against.
# ---------------------------------------------------------------------------
log "Installing apt packages"
export DEBIAN_FRONTEND=noninteractive

# A proxy in front of the archive can return a sporadic 502 on individual
# packages, which fails the whole transaction. Retry inside apt, and retry apt
# itself; already-fetched debs stay cached so a retry is cheap.
apt_retry() {
    local attempt
    for attempt in 1 2 3; do
        if apt-get -o Acquire::Retries=5 "$@"; then
            return 0
        fi
        echo "apt-get $1 failed (attempt ${attempt}/3); retrying" >&2
        sleep 5
    done
    return 1
}

# deb-src, so scripts/vendor-thirdparty.sh can get zlib's source through apt.
# It needs to: where TLS is intercepted a large HTTPS download can come back
# truncated, so neither the emscripten zlib port nor zlib.net's tarball can be
# fetched, and apt is unaffected because it talks plain HTTP to the archive.
UBUNTU_SOURCES=/etc/apt/sources.list.d/ubuntu.sources
if [ -f "${UBUNTU_SOURCES}" ] && ! grep -qE "^Types:.*deb-src" "${UBUNTU_SOURCES}"; then
    sed -i -E 's/^Types: deb[[:space:]]*$/Types: deb deb-src/' "${UBUNTU_SOURCES}"
fi

apt_retry update -qq
apt_retry install -y --no-install-recommends \
    brotli \
    ccache \
    dpkg-dev \
    build-essential \
    ca-certificates \
    curl \
    file \
    git \
    pkg-config \
    poppler-utils \
    imagemagick \
    rsync \
    sudo \
    unzip \
    xz-utils \
    zip \
    zlib1g-dev \
    libfreetype-dev \
    libharfbuzz-dev \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-declarative-dev \
    qt6-tools-dev \
    libqt6svg6-dev \
    libgl1-mesa-dev \
    libvulkan-dev \
    libxkbcommon-dev
apt-get clean
rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# TLS. On a plain network nothing needs doing. Where a proxy intercepts TLS,
# no https:// host is reachable until the distro trusts the same roots Windows
# does -- apt still works, because it talks plain HTTP to archive.ubuntu.com,
# which is why the failure only shows up later.
#
# So: probe, and only install the Windows roots if the probe says we must.
# ---------------------------------------------------------------------------
https_works() {
    curl -fsS --max-time 20 -o /dev/null https://mise.jdx.dev/ 2>/dev/null
}

install_windows_cas() {
    install -d /usr/local/share/ca-certificates/windows
    rm -f /usr/local/share/ca-certificates/windows/*.crt
    local pem
    for pem in "${WIN_CA_DIR}"/*.crt; do
        [ -e "${pem}" ] || continue
        sed 's/\r$//' "${pem}" >"/usr/local/share/ca-certificates/windows/$(basename "${pem}")"
    done
    update-ca-certificates

    # Tools that ship their own trust store (mise is Rust, emsdk drives Python,
    # node, cargo) will not look at the system store unless told to.
    cat >/etc/profile.d/10-corporate-ca.sh <<'EOF'
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
export SSL_CERT_DIR=/etc/ssl/certs
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
export REQUESTS_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
export NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt
export CARGO_HTTP_CAINFO=/etc/ssl/certs/ca-certificates.crt
EOF
    chmod 0644 /etc/profile.d/10-corporate-ca.sh
    # shellcheck disable=SC1091
    . /etc/profile.d/10-corporate-ca.sh
}

log "Probing HTTPS"
if https_works; then
    echo "HTTPS works with the stock trust store; no proxy CA needed."
    rm -f /etc/profile.d/10-corporate-ca.sh
    rm -rf /usr/local/share/ca-certificates/windows
    update-ca-certificates --fresh >/dev/null 2>&1 || true
elif [ -n "${WIN_CA_DIR}" ] && [ -d "${WIN_CA_DIR}" ]; then
    echo "HTTPS failed; installing the Windows root CAs from ${WIN_CA_DIR}"
    install_windows_cas
    if https_works; then
        echo "HTTPS works now."
    else
        echo "ERROR: HTTPS still fails after installing the Windows root CAs." >&2
        echo "       Check the proxy, or whether this host needs an HTTP(S)_PROXY setting." >&2
        exit 1
    fi
else
    echo "ERROR: HTTPS does not work and no Windows CA directory was supplied." >&2
    echo "       Re-run without -CorporateCA Never so the Windows roots can be exported." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# The dev user.
# ---------------------------------------------------------------------------
if id -u "${DEV_USER}" >/dev/null 2>&1; then
    log "User ${DEV_USER} already exists"
else
    log "Creating user ${DEV_USER}"
    useradd --create-home --shell /bin/bash --uid 1000 "${DEV_USER}"
    usermod -aG sudo "${DEV_USER}"
fi
printf '%s ALL=(ALL) NOPASSWD:ALL\n' "${DEV_USER}" >"/etc/sudoers.d/90-${DEV_USER}"
chmod 0440 "/etc/sudoers.d/90-${DEV_USER}"

# ---------------------------------------------------------------------------
# mise, and everything mise can provide.
# ---------------------------------------------------------------------------
log "Installing mise and the mise-managed toolchain as ${DEV_USER}"
sudo -u "${DEV_USER}" -H bash -euo pipefail <<'USERSETUP'
export MISE_YES=1

# /etc/profile.d is not read by this non-login shell.
if [ -f /etc/profile.d/10-corporate-ca.sh ]; then
    . /etc/profile.d/10-corporate-ca.sh
fi

if [ ! -x "$HOME/.local/bin/mise" ]; then
    curl -fsSL https://mise.run | sh
fi
export PATH="$HOME/.local/bin:$PATH"

if ! grep -q 'mise activate bash' "$HOME/.bashrc" 2>/dev/null; then
    cat >>"$HOME/.bashrc" <<'BASHRC'

# Corporate CA bundle (also set in /etc/profile.d, repeated for non-login shells)
if [ -f /etc/profile.d/10-corporate-ca.sh ]; then
    . /etc/profile.d/10-corporate-ca.sh
fi

# mise
export PATH="$HOME/.local/bin:$PATH"
eval "$(mise activate bash)"
BASHRC
fi

mise trust --all || true
mise use -g -y cmake@latest ninja@latest python@3.12 emsdk@latest

mise install -y
mise reshim || true
USERSETUP

# Ubuntu's default .bashrc returns early for non-interactive shells, so the
# `mise activate` line there never runs for `wsl -- bash -lc '...'`. Put the
# shims on PATH from profile.d instead, which every login shell reads.
log "Exposing mise shims to non-interactive login shells"
cat >/etc/profile.d/20-mise.sh <<'EOF'
export PATH="$HOME/.local/bin:$HOME/.local/share/mise/shims:$PATH"
EOF
chmod 0644 /etc/profile.d/20-mise.sh

# ---------------------------------------------------------------------------
# ccache. The root CMakeLists finds it and compiles through it; the default
# 5GB limit is too small for a tree this size held in several configurations
# at once, and the point of the cache is being able to flip between them.
# ---------------------------------------------------------------------------
log "Sizing the ccache"
su - "${DEV_USER}" -c 'ccache --max-size=20G' >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# Report what we ended up with.
# ---------------------------------------------------------------------------
log "Toolchain versions"
sudo -u "${DEV_USER}" -H bash -lc '
    set +e
    echo "mise     : $(mise --version 2>&1 | head -1)"
    echo "gcc      : $(gcc --version | head -1)"
    echo "cmake    : $(mise exec -- cmake --version | head -1)"
    echo "ninja    : $(mise exec -- ninja --version)"
    echo "python   : $(mise exec -- python --version)"
    echo "emcc     : $(mise exec -- emcc --version 2>&1 | head -1)"
    echo "qmake6   : $(qmake6 -query QT_VERSION 2>&1)"
    echo "freetype : $(pkg-config --modversion freetype2)"
    echo "harfbuzz : $(pkg-config --modversion harfbuzz)"
'

log "Done"
