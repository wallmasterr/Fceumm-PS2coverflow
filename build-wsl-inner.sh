#!/usr/bin/env bash
# Called by build-wsl.bat with argument: build | nopack | clean
set -euo pipefail
cd "$(dirname "$0")"

if [[ -f ./wsl-ps2env.sh ]]; then
  # shellcheck disable=SC1091
  source ./wsl-ps2env.sh
fi

if [[ -z "${PS2SDK:-}" ]]; then
  echo "PS2SDK is not set in this WSL session."
  echo ""
  echo "That is why make looked for \`/samples/Makefile.eeglobal\` (empty PS2SDK)."
  echo "Fix one of these:"
  echo "  1) Copy wsl-ps2env.sh.example to wsl-ps2env.sh and set PS2SDK + GSKIT paths."
  echo "  2) Add to ~/.bashrc (after installing ps2toolchain + gsKit):"
  echo "       export PS2SDK=\"\$HOME/ps2dev/ps2sdk\""
  echo "       export GSKIT=\"\$HOME/ps2dev/gsKit\""
  echo "     Then run: source ~/.bashrc"
  echo ""
  echo "Install toolchain: https://github.com/ps2dev/ps2toolchain"
  exit 1
fi

if [[ ! -f "$PS2SDK/samples/Makefile.eeglobal" ]]; then
  echo "PS2SDK is set but this file is missing:"
  echo "  $PS2SDK/samples/Makefile.eeglobal"
  echo ""
  if [[ ! -d "$PS2SDK" ]]; then
    echo "The directory \$PS2SDK does not exist. Build/install ps2toolchain first:"
    echo "  https://github.com/ps2dev/ps2toolchain"
  elif [[ -f "$PS2SDK/samples/Makefile.eeglobal_sample" ]]; then
    echo "You likely pointed PS2SDK at the ps2sdk SOURCE checkout."
    echo "After \"make install\" for ps2sdk, the real files are under your install prefix."
    echo "Try the same path you use when compiling other PS2 homebrew, or run from your"
    echo "ps2sdk source tree (with PS2SDK set to that install prefix):"
    echo "  cd /path/to/ps2sdk && make release-samples"
    echo "(That copies Makefile.eeglobal_sample -> \$PS2SDK/samples/Makefile.eeglobal)"
  else
    echo "Listing \$PS2SDK/samples (if any):"
    ls -la "$PS2SDK/samples" 2>/dev/null || echo "  (no samples directory)"
  fi
  echo ""
  echo "Quick checks (run in WSL):"
  echo "  ls -la \"\$PS2SDK/samples/Makefile.eeglobal\""
  echo "  which mips64r5900el-ps2-elf-gcc"
  exit 1
fi

if [[ -z "${GSKIT:-}" ]]; then
  echo "GSKIT is not set. Example:"
  echo "  export GSKIT=\"\$HOME/ps2dev/gsKit\""
  echo "Build gsKit from: https://github.com/ps2dev/gsKit"
  exit 1
fi

case "${1:-build}" in
  clean)
    make clean
    ;;
  nopack)
    make -j"$(nproc)" NOT_PACKED=1
    echo
    ls -la fceu.elf fceu-packed.elf 2>/dev/null || true
    ;;
  build)
    make -j"$(nproc)"
    echo
    ls -la fceu.elf fceu-packed.elf 2>/dev/null || true
    ;;
  *)
    echo "Usage: $0 {build|nopack|clean}" >&2
    exit 1
    ;;
esac
