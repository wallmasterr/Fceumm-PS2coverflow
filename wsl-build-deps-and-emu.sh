#!/usr/bin/env bash
# Run from WSL: builds ps2sdk-ports (cmakelibs), gsKit, then this emulator.
# Requires: ps2toolchain (ee/iop/dvp) + ps2sdk installed at $PS2SDK (make release from ps2sdk-src).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
[[ -f "$HOME/.bashrc" ]] && source "$HOME/.bashrc"
if [[ -f "$SCRIPT_DIR/wsl-ps2env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$SCRIPT_DIR/wsl-ps2env.sh"
fi

export PS2DEV="${PS2DEV:-$HOME/ps2dev}"
export PS2SDK="${PS2SDK:-$PS2DEV/ps2sdk}"
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin"

if [[ ! -f "$PS2SDK/samples/Makefile.eeglobal" ]]; then
  echo "Missing: $PS2SDK/samples/Makefile.eeglobal"
  echo "Install ps2toolchain, then clone ps2sdk and from ps2sdk-src run: make && make release"
  exit 1
fi

# ps2sdk-ports cmakelibs uses ports that require CMake 3.30+; Ubuntu/WSL often ships 3.27–3.28.
need_cmake="3.30"
if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. ps2sdk-ports needs CMake $need_cmake or newer."
  exit 1
fi
have_cmake=$(cmake --version | head -n1 | sed -n 's/.* \([0-9][0-9.]*\).*/\1/p')
first=$(printf '%s\n' "$have_cmake" "$need_cmake" | sort -V | head -n1)
if [[ "$first" != "$need_cmake" ]]; then
  echo "CMake $have_cmake is too old; ps2sdk-ports needs $need_cmake+ (you hit this in make cmakelibs)."
  echo ""
  if [[ -r /etc/os-release ]]; then
    # shellcheck source=/dev/null
    . /etc/os-release
    codename="${VERSION_CODENAME:-}"
    echo "Upgrade on Ubuntu/WSL (pick your release codename: jammy=22.04, noble=24.04):"
    echo "  wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null"
    echo "  echo \"deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ${codename:-jammy} main\" | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null"
    echo "  sudo apt-get update && sudo apt-get install -y cmake"
    echo ""
    echo "This machine reports VERSION_CODENAME=$codename — if empty, use jammy or noble explicitly."
  else
    echo "Install CMake $need_cmake+ from https://cmake.org/download/ or your distro’s backports."
  fi
  exit 1
fi

echo "=== ps2sdk-ports (cmakelibs) — first run takes a long time ==="
cd "$HOME"
if [[ ! -d ps2sdk-ports ]]; then
  git clone https://github.com/ps2dev/ps2sdk-ports.git
fi
cd ps2sdk-ports
git pull --ff-only 2>/dev/null || true

# Help curl's CMake find wolfSSL in $PS2SDK/ports (not host OpenSSL/GnuTLS/MbedTLS).
export PKG_CONFIG_PATH="$PS2SDK/ports/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# curl 8.x may still probe host TLS libs; upstream uses wolfSSL only — force others off.
if [[ -f build-cmakelibs.sh ]] && grep -q 'build_ee curl -D' build-cmakelibs.sh \
  && ! grep -q 'CURL_USE_GNUTLS=OFF' build-cmakelibs.sh; then
  sed -i.bak 's/build_ee curl /build_ee curl -DCURL_USE_GNUTLS=OFF -DCURL_USE_MBEDTLS=OFF /' build-cmakelibs.sh
fi

make cmakelibs

echo "=== gsKit ==="
mkdir -p "$(dirname "$GSKIT")"
if [[ ! -f "$GSKIT/Makefile" ]]; then
  if [[ -e "$GSKIT" ]] && [[ -n "$(ls -A "$GSKIT" 2>/dev/null || true)" ]]; then
    echo "ERROR: $GSKIT exists but is not a gsKit tree (no Makefile)."
    echo "Remove it and re-run, e.g.: rm -rf \"$GSKIT\""
    exit 1
  fi
  git clone https://github.com/ps2dev/gsKit.git "$GSKIT"
fi
cd "$GSKIT"
make
make install

echo "=== FCEUmm-PS2 (nopack) ==="
cd "$SCRIPT_DIR"
bash ./build-wsl-inner.sh nopack

echo "Done: $SCRIPT_DIR/fceu.elf"
