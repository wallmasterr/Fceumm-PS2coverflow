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

echo "=== ps2sdk-ports (cmakelibs) — first run takes a long time ==="
cd "$HOME"
if [[ ! -d ps2sdk-ports ]]; then
  git clone https://github.com/ps2dev/ps2sdk-ports.git
fi
cd ps2sdk-ports
make cmakelibs

echo "=== gsKit ==="
cd "$PS2DEV"
if [[ ! -d gsKit ]]; then
  git clone https://github.com/ps2dev/gsKit.git
fi
cd gsKit
make
make install

echo "=== FCEUmm-PS2 (nopack) ==="
cd "$SCRIPT_DIR"
bash ./build-wsl-inner.sh nopack

echo "Done: $SCRIPT_DIR/fceu.elf"
