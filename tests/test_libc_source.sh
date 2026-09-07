#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
build_path=${BUILD_PATH:-build}
source_file=commands/wc.c
object_file=$build_path/wc_command.o
archive_file=$build_path/libcannedbsd.a

if [[ ! -f $source_file ]]; then
    echo "FAIL: external ordinary-main source is missing: $source_file" >&2
    exit 1
fi
if [[ ! -f $object_file ]]; then
    echo "FAIL: external command was not compiled separately: $object_file" >&2
    exit 1
fi
if [[ ! -f $archive_file ]] ||
        ! ar t "$archive_file" | rg -q '^cb_libc\.o$'; then
    echo "FAIL: libc archive is missing or malformed: $archive_file" >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$source_file"; then
    echo 'FAIL: ordinary command source depends on cannedBSD-specific names' >&2
    exit 1
fi
for interface in read write open malloc free; do
    if ! rg -q "\\b${interface}[[:space:]]*\\(" "$source_file"; then
        printf 'FAIL: ordinary command does not exercise %s()\n' \
            "$interface" >&2
        exit 1
    fi
done
if ! nm "$object_file" | rg -q '[[:space:]]T[[:space:]]+cb_wc_main$'; then
    echo 'FAIL: startup build did not rename ordinary main' >&2
    exit 1
fi
if nm "$object_file" | rg -q '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: external command exports the enclosing application main' >&2
    exit 1
fi
for symbol in read write open close malloc free; do
    if nm -u "$object_file" | rg -q "[[:space:]]U[[:space:]]+${symbol}$"; then
        printf 'FAIL: command object imports host-facing %s instead of the prefixed veneer\n' \
            "$symbol" >&2
        exit 1
    fi
done

echo 'external libc source boundary passed'
