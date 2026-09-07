#!/usr/bin/env bash
set -euo pipefail
cmake -S platform/mac68k -B build-mac68k \
    -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake --build build-mac68k -j2
test -s build-mac68k/CannedBSD.bin
test -s build-mac68k/.rsrc/CannedBSD.APPL
# The dedicated runner mounts its artifact store here. Keep each CI run separate.
artifact_root="/artifacts/${CI_PIPELINE_NUMBER:?}-${CI_COMMIT_SHA:?}"
mkdir -p "$artifact_root"
artifact_dir=$(mktemp -d "$artifact_root/run.XXXXXX")
tar -C build-mac68k -czf "$artifact_dir/CannedBSD.tar.gz" \
    CannedBSD.bin CannedBSD.dsk CannedBSD.APPL .rsrc .finf
printf '%s\n' "$CI_COMMIT_SHA" > "$artifact_dir/commit.txt"
(cd "$artifact_dir" && sha256sum CannedBSD.tar.gz > SHA256SUMS)
printf 'Mac application artifact: %s\n' "$artifact_dir"
