#!/usr/bin/env bash
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
image=ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019
exec docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    -e HOME=/tmp -v "$project_dir:/workspace" -w /workspace "$image" \
    bash -c 'cmake -S platform/mac68k -B build-mac68k -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake && cmake --build build-mac68k -j2'
