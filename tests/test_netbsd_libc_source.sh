#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
build_path=${BUILD_PATH:-build}
source_file=upstream/netbsd/common/lib/libc/string/strlen.c
object_file=$build_path/netbsd_strlen.o
archive_file=$build_path/libcannedbsd.a
expected_hash=08969942df6b9b53bb3500e39ec47f514b876809202383088ac9d36e007d64e1
expected_revision=b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c
provenance_file=UPSTREAM.md

if [[ ! -f $source_file ]]; then
    echo "FAIL: pinned NetBSD strlen source is missing: $source_file" >&2
    exit 1
fi
actual_hash=$(sha256sum "$source_file" | awk '{print $1}')
if [[ $actual_hash != "$expected_hash" ]]; then
    printf 'FAIL: NetBSD strlen source changed: expected %s, found %s\n' \
        "$expected_hash" "$actual_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! rg -q "$expected_revision" "$provenance_file" ||
        ! rg -q "$expected_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strlen provenance is absent or does not match the pin' >&2
    exit 1
fi
if rg -q '\bassert[[:space:]]*\(' "$source_file"; then
    echo 'FAIL: the import-only assert shim is unsafe for this source' >&2
    exit 1
fi
if [[ ! -f $object_file ]] ||
        ! nm "$object_file" | rg -q '[[:space:]]T[[:space:]]+cb_libc_strlen$'; then
    echo 'FAIL: NetBSD strlen was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$object_file" | rg -q '[[:space:]]U[[:space:]]+strlen$'; then
    echo 'FAIL: NetBSD strlen object imports host strlen' >&2
    exit 1
fi
if [[ ! -f $archive_file ]] ||
        ! ar t "$archive_file" | rg -q '^netbsd_strlen\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strlen' >&2
    exit 1
fi
if ! nm -u "$build_path/cb_libc.o" |
        rg -q '[[:space:]]U[[:space:]]+cb_libc_strlen$'; then
    echo 'FAIL: cannedBSD puts does not use NetBSD strlen' >&2
    exit 1
fi

echo 'pinned unmodified NetBSD libc source boundary passed'
