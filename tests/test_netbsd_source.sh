#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
build_path=${BUILD_PATH:-build}
source_file=upstream/netbsd/usr.bin/yes/yes.c
object_file=$build_path/netbsd_yes.o
expected_hash=f57930fc157302e11ea0ec67e3afe42b96c512d0f3243a2fde5425bd5c9812d8
expected_revision=b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c
provenance_file=UPSTREAM.md

if [[ ! -f $source_file ]]; then
    echo "FAIL: pinned NetBSD source is missing: $source_file" >&2
    exit 1
fi
actual_hash=$(sha256sum "$source_file" | awk '{print $1}')
if [[ $actual_hash != "$expected_hash" ]]; then
    printf 'FAIL: NetBSD yes source changed: expected %s, found %s\n' \
        "$expected_hash" "$actual_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! rg -q "$expected_revision" "$provenance_file" ||
        ! rg -q "$expected_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD yes provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $object_file ]] ||
        ! nm "$object_file" | rg -q '[[:space:]]T[[:space:]]+cb_yes_main$'; then
    echo "FAIL: pinned NetBSD yes was not compiled as a command object" >&2
    exit 1
fi
if nm -u "$object_file" | rg -q '[[:space:]]U[[:space:]]+puts$'; then
    echo 'FAIL: NetBSD yes imports host puts instead of cannedBSD libc' >&2
    exit 1
fi
if ! nm -u "$object_file" |
        rg -q '[[:space:]]U[[:space:]]+cb_libc_puts$'; then
    echo 'FAIL: NetBSD yes does not import cannedBSD libc puts' >&2
    exit 1
fi

echo 'pinned unmodified NetBSD source boundary passed'
