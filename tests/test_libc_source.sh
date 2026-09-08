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
exit_source=tests/libc_exit_probe.c
exit_object=$build_path/exitprobe_command.o
exit_header=libc/include/stdlib.h
stdio_source=tests/libc_stdio_source.c
stdio_object=$build_path/libc_stdio_source.o
getopt_source=tests/libc_getopt_probe.c
getopt_object=$build_path/getoptprobe_command.o
truncate_source=tests/libc_truncate_probe.c
truncate_object=$build_path/libc_truncate_probe.o
errx_source=tests/libc_errx_probe.c
errx_object=$build_path/errxprobe_command.o
dirent_source=tests/libc_dirent_probe.c
dirent_object=$build_path/direntprobe_command.o

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
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$exit_source"; then
    echo 'FAIL: exit source probe uses cannedBSD-specific names' >&2
    exit 1
fi
if ! matches '\bexit[[:space:]]*\(' "$exit_source"; then
    echo 'FAIL: exit source probe does not call exit()' >&2
    exit 1
fi
if nm -u "$exit_object" | matches '[[:space:]]U[[:space:]]+exit$'; then
    echo 'FAIL: exit probe imports a host-facing exit symbol' >&2
    exit 1
fi
if ! nm -u "$exit_object" |
        matches '[[:space:]]U[[:space:]]+cb_libc_exit$'; then
    echo 'FAIL: exit probe does not use the private veneer function' >&2
    exit 1
fi
if ! matches 'cb_libc_exit\([^;]*\)\s*__dead' "$exit_header"; then
    echo 'FAIL: exit declaration does not carry __dead metadata' >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$stdio_source"; then
    echo 'FAIL: stdio source probe uses cannedBSD-specific names' >&2
    exit 1
fi
for symbol in printf fprintf; do
    if ! matches "\\b${symbol}[[:space:]]*\\(" "$stdio_source" ||
            nm -u "$stdio_object" |
                matches "[[:space:]]U[[:space:]]+${symbol}$" ||
            ! nm -u "$stdio_object" |
                matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$"; then
        printf 'FAIL: ordinary %s source does not use the private veneer\n' \
            "$symbol" >&2
        exit 1
    fi
done
for stream in stdout stderr; do
    if ! matches "\\b${stream}\\b" "$stdio_source" ||
            ! nm -u "$stdio_object" |
                matches "[[:space:]]U[[:space:]]+cb_libc_${stream}_stream$"; then
        printf 'FAIL: ordinary stdio source does not use private %s\n' \
            "$stream" >&2
        exit 1
    fi
done
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$getopt_source"; then
    echo 'FAIL: getopt source probe uses cannedBSD-specific names' >&2
    exit 1
fi
if ! matches '\bgetopt[[:space:]]*\(' "$getopt_source"; then
    echo 'FAIL: getopt source probe does not call getopt()' >&2
    exit 1
fi
if nm -u "$getopt_object" | matches '[[:space:]]U[[:space:]]+getopt$'; then
    echo 'FAIL: getopt probe imports a host-facing getopt symbol' >&2
    exit 1
fi
if ! nm -u "$getopt_object" |
        matches '[[:space:]]U[[:space:]]+cb_libc_getopt$'; then
    echo 'FAIL: getopt probe does not use the private veneer function' >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$errx_source"; then
    echo 'FAIL: errx source probe uses cannedBSD-specific names' >&2
    exit 1
fi
if ! matches '\berrx[[:space:]]*\(' "$errx_source"; then
    echo 'FAIL: errx source probe does not call errx()' >&2
    exit 1
fi
if nm -u "$errx_object" | matches '[[:space:]]U[[:space:]]+errx$'; then
    echo 'FAIL: errx probe imports a host-facing errx symbol' >&2
    exit 1
fi
if ! nm -u "$errx_object" |
        matches '[[:space:]]U[[:space:]]+cb_libc_errx$'; then
    echo 'FAIL: errx probe does not use the private veneer function' >&2
    exit 1
fi
if rg -n 'cannedbsd|internal\.h|\bcb_[A-Za-z0-9_]+' "$dirent_source"; then
    echo 'FAIL: dirent source probe uses cannedBSD-specific names' >&2
    exit 1
fi
for interface in opendir readdir closedir; do
    if ! matches "\\b${interface}[[:space:]]*\\(" "$dirent_source"; then
        printf 'FAIL: dirent source probe does not call %s()\n' \
            "$interface" >&2
        exit 1
    fi
    if nm -u "$dirent_object" | matches "[[:space:]]U[[:space:]]+${interface}\$"; then
        printf 'FAIL: dirent probe imports host-facing %s\n' "$interface" >&2
        exit 1
    fi
    if ! nm -u "$dirent_object" |
            matches "[[:space:]]U[[:space:]]+cb_libc_${interface}\$"; then
        printf 'FAIL: dirent probe does not use the private veneer %s\n' \
            "$interface" >&2
        exit 1
    fi
done
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
