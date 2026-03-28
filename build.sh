#!/usr/bin/env bash
# Run from WSL/Linux (same checks as build-wsl.bat via build-wsl-inner.sh).
cd "$(dirname "$0")"
case "${1:-}" in
  clean)
    exec bash build-wsl-inner.sh clean
    ;;
  nopack)
    exec bash build-wsl-inner.sh nopack
    ;;
  "")
    exec bash build-wsl-inner.sh build
    ;;
  *)
    echo "Usage: $0 [clean|nopack]" >&2
    exit 1
    ;;
esac
