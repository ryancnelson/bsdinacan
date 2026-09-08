#!/usr/bin/env bash
set -euo pipefail

# Consume the complete producer output: early exit with rg -q can give nm/ar
# SIGPIPE, which pipefail would misreport as a missing symbol.
matches() { rg "$@" > /dev/null; }

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
build_path=${BUILD_PATH:-build}
source_file=commands/wc.c
object_file=$build_path/wc_command.o
archive_file=$build_path/libcannedbsd.a
errno_header=libc/include/errno.h
allocation_source=tests/libc_allocation_source.c
allocation_object=$build_path/libc_allocation_source.o
memory_source=tests/libc_memory_source.c
memory_object=$build_path/libc_memory_source.o
environ_source=tests/libc_environ_source.c
environ_object=$build_path/libc_environ_source.o
truncate_source=tests/libc_truncate_probe.c
truncate_object=$build_path/libc_truncate_probe.o

if [[ ! -f $source_file ]]; then
    echo "FAIL: external ordinary-main source is missing: $source_file" >&2
    exit 1
fi
if [[ ! -f $object_file ]]; then
    echo "FAIL: external command was not compiled separately: $object_file" >&2
    exit 1
fi
if [[ ! -f $archive_file ]] ||
        ! ar t "$archive_file" | matches '^cb_libc\.o$'; then
    echo "FAIL: libc archive is missing or malformed: $archive_file" >&2
    exit 1
fi
if [[ ! -f $errno_header ]]; then
    echo "FAIL: libc errno header is missing: $errno_header" >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$source_file"; then
    echo 'FAIL: ordinary command source depends on cannedBSD-specific names' >&2
    exit 1
fi
for interface in read write open malloc free strerror strlen strcmp; do
    if ! matches "\\b${interface}[[:space:]]*\\(" "$source_file"; then
        printf 'FAIL: ordinary command does not exercise %s()\n' \
            "$interface" >&2
        exit 1
    fi
done
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$memory_source"; then
    echo 'FAIL: memory source probe uses cannedBSD-specific names' >&2
    exit 1
fi
if ! matches '\bmemcpy[[:space:]]*\(' "$memory_source" ||
        nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+memcpy$' ||
        ! nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+cb_libc_memcpy$'; then
    echo 'FAIL: ordinary memcpy source does not use the private veneer' >&2
    exit 1
fi
if ! matches '\bmemmove[[:space:]]*\(' "$memory_source" ||
        nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+memmove$' ||
        ! nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+cb_libc_memmove$'; then
    echo 'FAIL: ordinary memmove source does not use the private veneer' >&2
    exit 1
fi
if ! matches '\bmemcmp[[:space:]]*\(' "$memory_source" ||
        nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+memcmp$' ||
        ! nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+cb_libc_memcmp$'; then
    echo 'FAIL: ordinary memcmp source does not use the private veneer' >&2
    exit 1
fi
if ! matches '\bstrchr[[:space:]]*\(' "$memory_source" ||
        nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+strchr$' ||
        ! nm -u "$memory_object" |
            matches '[[:space:]]U[[:space:]]+cb_libc_strchr$'; then
    echo 'FAIL: ordinary strchr source does not use the private veneer' >&2
    exit 1
fi
if ! matches '\berrno\b' "$source_file"; then
    echo 'FAIL: ordinary command does not exercise the errno lvalue' >&2
    exit 1
fi
if ! nm "$object_file" | matches '[[:space:]]T[[:space:]]+cb_wc_main$'; then
    echo 'FAIL: startup build did not rename ordinary main' >&2
    exit 1
fi
if nm "$object_file" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: external command exports the enclosing application main' >&2
    exit 1
fi
for symbol in read write open close malloc free strerror strlen strcmp; do
    if nm -u "$object_file" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        printf 'FAIL: command object imports host-facing %s instead of the prefixed veneer\n' \
            "$symbol" >&2
        exit 1
    fi
done
for symbol in cb_libc_strlen cb_libc_strcmp; do
    if ! nm -u "$object_file" |
            matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        printf 'FAIL: command object does not import %s\n' "$symbol" >&2
        exit 1
    fi
done
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$environ_source"; then
    echo 'FAIL: environ source probe uses cannedBSD-specific names' >&2
    exit 1
fi
if ! matches '\benviron\b' "$environ_source"; then
    echo 'FAIL: environ source probe does not reference environ' >&2
    exit 1
fi
if nm -u "$environ_object" | matches '[[:space:]]U[[:space:]]+environ$'; then
    echo 'FAIL: environ probe imports a host-facing environ symbol' >&2
    exit 1
fi
if ! nm -u "$environ_object" |
        matches '[[:space:]]U[[:space:]]+cb_libc_environ_location$'; then
    echo 'FAIL: environ probe does not use the private veneer accessor' >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$allocation_source"; then
    echo 'FAIL: allocation source probe uses cannedBSD-specific names' >&2
    exit 1
fi
for symbol in calloc realloc; do
    if ! matches "\b${symbol}[[:space:]]*\(" "$allocation_source"; then
        printf 'FAIL: allocation source probe does not call %s\n' "$symbol" >&2
        exit 1
    fi
    if nm -u "$allocation_object" |
            matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        printf 'FAIL: allocation probe imports host-facing %s\n' "$symbol" >&2
        exit 1
    fi
    if ! nm -u "$allocation_object" |
            matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$"; then
        printf 'FAIL: allocation probe does not import cb_libc_%s\n' \
            "$symbol" >&2
        exit 1
    fi
done

if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$truncate_source"; then
    echo 'FAIL: truncate probe uses private names' >&2
    exit 1
fi
for symbol in truncate ftruncate; do
    if ! matches "\\b${symbol}[[:space:]]*\\(" "$truncate_source" ||
            nm -u "$truncate_object" | matches "[[:space:]]U[[:space:]]+${symbol}$" ||
            ! nm -u "$truncate_object" | matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$"; then
        printf 'FAIL: ordinary %s probe does not use the private veneer\n' "$symbol" >&2
        exit 1
    fi
done

echo 'external libc source boundary passed'
