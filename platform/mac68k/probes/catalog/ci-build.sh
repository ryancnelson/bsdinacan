#!/usr/bin/env bash
set -euo pipefail
: "${CI_COMMIT_SHA:?}" "${CI_PIPELINE_NUMBER:?}"
build_dir=$(mktemp -d /tmp/morefiles-build.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
python3 platform/mac68k/probes/catalog/build_fixture.py "$build_dir/fixture" 2>&1 | tee "$build_dir/fixture-build.log"
cmake -S platform/mac68k/probes/catalog -B "$build_dir/app" \
    -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
    -DFIXTURE_INCLUDE="$build_dir/fixture" -DPROBE_COMMIT="$CI_COMMIT_SHA" 2>&1 | tee "$build_dir/configure.log"
cmake --build "$build_dir/app" -j2 2>&1 | tee "$build_dir/build.log"
python3 platform/mac68k/check_code_resources.py "$build_dir/app/.rsrc/CannedBSD.APPL"
/Retro68-build/toolchain/bin/m68k-apple-macos-size "$build_dir/app/CannedBSD.code.bin.gdb" > "$build_dir/code-size.txt"
artifact_root="/artifacts/$CI_PIPELINE_NUMBER-$CI_COMMIT_SHA"
mkdir -p "$artifact_root"
artifact_dir=$(mktemp -d "$artifact_root/morefiles-probe.XXXXXX")
chmod 755 "$artifact_dir"
cp "$build_dir/fixture/CatalogFixture.dsk" "$build_dir/fixture/fixture.json" \
    "$build_dir/fixture/FIXTURE.SHA256" "$artifact_dir/"
chmod 444 "$artifact_dir/CatalogFixture.dsk"
printf '%s\n' "$CI_COMMIT_SHA" > "$artifact_dir/commit.txt"
printf '%s\n' "$CI_PIPELINE_NUMBER" > "$artifact_dir/pipeline.txt"
cp "$build_dir/fixture-build.log" "$build_dir/configure.log" "$build_dir/build.log" \
    "$build_dir/code-size.txt" "$artifact_dir/"
cp platform/mac68k/probes/catalog/README.md "$artifact_dir/README.md"
tar -C "$build_dir/app" -czf "$artifact_dir/MoreFilesProbe.tar.gz" \
    CannedBSD.bin CannedBSD.dsk CannedBSD.APPL .rsrc .finf
(cd "$artifact_dir" && sha256sum MoreFilesProbe.tar.gz CatalogFixture.dsk fixture.json > SHA256SUMS)
echo "MoreFiles derivative COMPILED; guest acceptance still required: $artifact_dir"
