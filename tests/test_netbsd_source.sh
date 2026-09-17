#!/usr/bin/env bash
set -euo pipefail

# Consume the complete producer output: early exit with rg -q can give nm/ar
# SIGPIPE, which pipefail would misreport as a missing symbol.
matches() { rg "$@" > /dev/null; }

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
        ! matches "$expected_revision" "$provenance_file" ||
        ! matches "$expected_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD yes provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $object_file ]] ||
        ! nm "$object_file" | matches '[[:space:]]T[[:space:]]+cb_yes_main$'; then
    echo "FAIL: pinned NetBSD yes was not compiled as a command object" >&2
    exit 1
fi
if nm -u "$object_file" | matches '[[:space:]]U[[:space:]]+puts$'; then
    echo 'FAIL: NetBSD yes imports host puts instead of cannedBSD libc' >&2
    exit 1
fi
if ! nm -u "$object_file" |
        matches '[[:space:]]U[[:space:]]+cb_libc_puts$'; then
    echo 'FAIL: NetBSD yes does not import cannedBSD libc puts' >&2
    exit 1
fi

printenv_source=upstream/netbsd/usr.bin/printenv/printenv.c
printenv_object=$build_path/netbsd_printenv.o
printenv_hash=d355c07fc5a351d38e2f8552899b456f1300a61408ebf2e2af47c5f52de974db

if [[ ! -f $printenv_source ]]; then
    echo "FAIL: pinned NetBSD printenv source is missing: $printenv_source" >&2
    exit 1
fi
actual_printenv_hash=$(sha256sum "$printenv_source" | awk '{print $1}')
if [[ $actual_printenv_hash != "$printenv_hash" ]]; then
    printf 'FAIL: NetBSD printenv source changed: expected %s, found %s\n' \
        "$printenv_hash" "$actual_printenv_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! matches "$printenv_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD printenv provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $printenv_object ]] ||
        ! nm "$printenv_object" |
            matches '[[:space:]]T[[:space:]]+cb_printenv_main$'; then
    echo "FAIL: pinned NetBSD printenv was not compiled as a command object" >&2
    exit 1
fi
if nm "$printenv_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: NetBSD printenv exports the enclosing application main' >&2
    exit 1
fi
for host_symbol in environ getopt errx exit memcmp; do
    if nm -u "$printenv_object" |
            matches "[[:space:]]U[[:space:]]+${host_symbol}\$"; then
        printf 'FAIL: NetBSD printenv imports host-facing %s\n' \
            "$host_symbol" >&2
        exit 1
    fi
done
for private_symbol in cb_libc_environ_location cb_libc_getopt cb_libc_errx \
        cb_libc_exit cb_libc_memcmp; do
    if ! nm -u "$printenv_object" |
            matches "[[:space:]]U[[:space:]]+${private_symbol}\$"; then
        printf 'FAIL: NetBSD printenv does not import %s\n' \
            "$private_symbol" >&2
        exit 1
    fi
done


dirname_source=upstream/netbsd/usr.bin/dirname/dirname.c
dirname_object=$build_path/dirname_command.o
dirname_hash=839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9

if [[ ! -f $dirname_source ]]; then
    echo "FAIL: pinned NetBSD dirname source is missing: $dirname_source" >&2
    exit 1
fi
actual_dirname_hash=$(sha256sum "$dirname_source" | awk '{print $1}')
if [[ $actual_dirname_hash != "$dirname_hash" ]]; then
    printf 'FAIL: NetBSD dirname source changed: expected %s, found %s\n'         "$dirname_hash" "$actual_dirname_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! matches "$dirname_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD dirname provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $dirname_object ]] ||
        ! nm "$dirname_object" |
            matches '[[:space:]]T[[:space:]]+cb_dirname_main$'; then
    echo "FAIL: pinned NetBSD dirname was not compiled as a command object" >&2
    exit 1
fi
if nm "$dirname_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: NetBSD dirname exports the enclosing application main' >&2
    exit 1
fi
for host_symbol in dirname setlocale getopt exit printf fprintf puts errx err; do
    if nm -u "$dirname_object" |
            matches "[[:space:]]U[[:space:]]+${host_symbol}\$"; then
        printf 'FAIL: NetBSD dirname imports host-facing %s\n'             "$host_symbol" >&2
        exit 1
    fi
done
for private_symbol in cb_libc_dirname cb_libc_setlocale cb_libc_getopt \
        cb_libc_exit cb_libc_printf cb_libc_fprintf cb_libc_err; do
    if ! nm -u "$dirname_object" |
            matches "[[:space:]]U[[:space:]]+${private_symbol}\$"; then
        printf 'FAIL: NetBSD dirname does not import %s\n'             "$private_symbol" >&2
        exit 1
    fi
done


basename_source=upstream/netbsd/usr.bin/basename/basename.c
basename_object=$build_path/basename_command.o
basename_hash=717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd

if [[ ! -f $basename_source ]]; then
    echo "FAIL: pinned NetBSD basename source is missing: $basename_source" >&2
    exit 1
fi
actual_basename_hash=$(sha256sum "$basename_source" | awk '{print $1}')
if [[ $actual_basename_hash != "$basename_hash" ]]; then
    printf 'FAIL: NetBSD basename source changed: expected %s, found %s\n'         "$basename_hash" "$actual_basename_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! matches "$basename_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD basename provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $basename_object ]] ||
        ! nm "$basename_object" |
            matches '[[:space:]]T[[:space:]]+cb_basename_main$'; then
    echo "FAIL: pinned NetBSD basename was not compiled as a command object" >&2
    exit 1
fi
if nm "$basename_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: NetBSD basename exports the enclosing application main' >&2
    exit 1
fi
for host_symbol in basename setlocale getopt exit printf fprintf puts errx err \
        strlen strcmp; do
    if nm -u "$basename_object" |
            matches "[[:space:]]U[[:space:]]+${host_symbol}\$"; then
        printf 'FAIL: NetBSD basename imports host-facing %s\n'             "$host_symbol" >&2
        exit 1
    fi
done
for private_symbol in cb_libc_basename cb_libc_setlocale cb_libc_getopt \
        cb_libc_exit cb_libc_printf cb_libc_fprintf cb_libc_err \
        cb_libc_strlen cb_libc_strcmp; do
    if ! nm -u "$basename_object" |
            matches "[[:space:]]U[[:space:]]+${private_symbol}\$"; then
        printf 'FAIL: NetBSD basename does not import %s\n'             "$private_symbol" >&2
        exit 1
    fi
done

echo_source=upstream/netbsd/bin/echo/echo.c
echo_object=$build_path/netbsd_echo.o
echo_hash=06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04

if [[ ! -f $echo_source ]]; then
    echo "FAIL: pinned NetBSD echo source is missing: $echo_source" >&2
    exit 1
fi
actual_echo_hash=$(sha256sum "$echo_source" | awk '{print $1}')
if [[ $actual_echo_hash != "$echo_hash" ]]; then
    printf 'FAIL: NetBSD echo source changed: expected %s, found %s\n'         "$echo_hash" "$actual_echo_hash" >&2
    exit 1
fi
if [[ ! -f $provenance_file ]] ||
        ! matches "$echo_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD echo provenance is absent or does not match the pin' >&2
    exit 1
fi
if [[ ! -f $echo_object ]] ||
        ! nm "$echo_object" |
            matches '[[:space:]]T[[:space:]]+cb_netbsdecho_main$'; then
    echo "FAIL: pinned NetBSD echo was not compiled as a command object" >&2
    exit 1
fi
if nm "$echo_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: NetBSD echo exports the enclosing application main' >&2
    exit 1
fi
for host_symbol in setprogname setlocale strcmp printf putchar fflush \
        ferror err; do
    if nm -u "$echo_object" |
            matches "[[:space:]]U[[:space:]]+${host_symbol}\$"; then
        printf 'FAIL: NetBSD echo imports host-facing %s\n'             "$host_symbol" >&2
        exit 1
    fi
done
for private_symbol in cb_libc_setprogname cb_libc_setlocale cb_libc_strcmp \
        cb_libc_printf cb_libc_putchar cb_libc_fflush cb_libc_ferror \
        cb_libc_err; do
    if ! nm -u "$echo_object" |
            matches "[[:space:]]U[[:space:]]+${private_symbol}\$"; then
        printf 'FAIL: NetBSD echo does not import %s\n'             "$private_symbol" >&2
        exit 1
    fi
done

head_source=upstream/netbsd/usr.bin/head/head.c
head_object=$build_path/netbsd_head.o
head_hash=33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a
if [[ $(sha256sum "$head_source" | awk '{print $1}') != "$head_hash" ]] ||
   ! matches "$head_hash" "$provenance_file"; then
    echo 'FAIL: head source/provenance pin differs' >&2
    exit 1
fi
if ! nm "$head_object" | matches '[[:space:]]T[[:space:]]+cb_head_main$' ||
   nm "$head_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: head entry point is not privately renamed' >&2
    exit 1
fi
for symbol in err errno_location errx exit fclose feof fopen fprintf fread fwrite \
        getc getopt getopt_state_location getprogname isdigit malloc printf putchar \
        setlocale stderr_stream stdin_stream stdout_stream strcpy strlen strtoimax warn; do
    if ! nm -u "$head_object" | matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$" ||
       nm -u "$head_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: head private boundary for $symbol" >&2
        exit 1
    fi
done

mv_source=upstream/netbsd/bin/mv/mv.c
mv_pathnames=upstream/netbsd/bin/mv/pathnames.h
mv_object=$build_path/netbsd_mv.o
mv_hash=df5de897a14e2f8210140e468b94319eaf100018490bfebe564152bf337bfd54
mv_pathnames_hash=82819eb682b4e2e8ec84604d8b9b11532f987427d697989ff21c6aa56fc30d6a

if [[ $(sha256sum "$mv_source" | awk '{print $1}') != "$mv_hash" ]] ||
   ! matches "$mv_hash" "$provenance_file"; then
    echo 'FAIL: mv source/provenance pin differs' >&2
    exit 1
fi
if [[ $(sha256sum "$mv_pathnames" | awk '{print $1}') != "$mv_pathnames_hash" ]] ||
   ! matches "$mv_pathnames_hash" "$provenance_file"; then
    echo 'FAIL: mv pathnames source/provenance pin differs' >&2
    exit 1
fi
if ! nm "$mv_object" | matches '[[:space:]]T[[:space:]]+cb_mv_main$' ||
   nm "$mv_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: mv entry point is not privately renamed' >&2
    exit 1
fi
for symbol in access close errno_location errx execl exit fcpxattr fchflags fchmod fchown \
        fprintf futimes getchar getopt getopt_state_location getprogname group_from_gid \
        isatty lstat malloc memmove open printf read rename rmdir setlocale \
        setprogname signal stat stderr_stream strlcpy strlen strmode strrchr \
        unlink user_from_uid vfork waitpid warn warnx write; do
    if ! nm -u "$mv_object" | matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$" ||
       nm -u "$mv_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: mv private boundary for $symbol" >&2
        exit 1
    fi
done

cat_source=upstream/netbsd/bin/cat/cat.c
cat_object=$build_path/netbsd_cat.o
cat_hash=2cc2ced0fcc6c143e1406e64cdd64ea768101fcd19b6dad531b911a611697cbd

if [[ $(sha256sum "$cat_source" | awk '{print $1}') != "$cat_hash" ]] ||
   ! matches "$cat_hash" "$provenance_file"; then
    echo 'FAIL: cat source/provenance pin differs' >&2
    exit 1
fi
if ! nm "$cat_object" | matches '[[:space:]]T[[:space:]]+cb_cat_main$' ||
   nm "$cat_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: cat entry point is not privately renamed' >&2
    exit 1
fi
for symbol in clearerr close err fclose fcntl ferror fileno fopen fprintf \
        fstat getc getopt getopt_state_location getprogname isascii \
        iscntrl malloc open putchar read setbuf setlocale setprogname \
        stderr_stream stdin_stream stdout_stream strcmp strtol toascii \
        warn warnx write; do
    if ! nm -u "$cat_object" | matches "[[:space:]]U[[:space:]]+cb_libc_${symbol}$" ||
       nm -u "$cat_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: cat private boundary for $symbol" >&2
        exit 1
    fi
done

cp_source=upstream/netbsd/bin/cp/cp.c
cp_extern=upstream/netbsd/bin/cp/extern.h
cp_utils_source=upstream/netbsd/bin/cp/utils.c
cp_object=$build_path/netbsd_cp.o
cp_utils_object=$build_path/netbsd_cp_utils.o
cp_hash=fef86b0fbc0161c436a5b6e4c8255c19e86cd9033b71f72f81a3963551d06613
cp_extern_hash=6299aea50a1f960547bb0f426b3b7bfaed614258aba488287c4efbdc074e5ff6
cp_utils_hash=d20b071192c52f559082183fda12692ea99b6c19beec5680a58ed3475ae99ca2

if [[ $(sha256sum "$cp_source" | awk '{print $1}') != "$cp_hash" ]] ||
   ! matches "$cp_hash" "$provenance_file"; then
    echo 'FAIL: cp source/provenance pin differs' >&2
    exit 1
fi
if [[ $(sha256sum "$cp_extern" | awk '{print $1}') != "$cp_extern_hash" ]] ||
   ! matches "$cp_extern_hash" "$provenance_file"; then
    echo 'FAIL: cp extern source/provenance pin differs' >&2
    exit 1
fi
if [[ $(sha256sum "$cp_utils_source" | awk '{print $1}') != "$cp_utils_hash" ]] ||
   ! matches "$cp_utils_hash" "$provenance_file"; then
    echo 'FAIL: cp utils source/provenance pin differs' >&2
    exit 1
fi
if ! nm "$cp_object" | matches '[[:space:]]T[[:space:]]+cb_cp_main$' ||
   nm "$cp_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: cp entry point is not privately renamed' >&2
    exit 1
fi
for symbol in chmod err errno_location errx exit fts_children fts_close \
        fts_open fts_read fts_set getopt getopt_state_location getprogname \
        getuid lstat mkdir printf setlocale setprogname signal stat \
        stderr_stream stdin_stream stdout_stream strlcpy strlen strrchr \
        umask warn warnx; do
    if nm -u "$cp_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: cp imports host-facing $symbol" >&2
        exit 1
    fi
done
for symbol in chflags chmod close errno_location fcpxattr fts_set \
        getprogname lchmod lchown link lutimens madvise mkfifo mknod \
        mmap munmap open read readlink setprogname stat stderr_stream \
        stdout_stream strcmp strlcpy strlen strncat symlink unlink warn \
        warnx write; do
    if nm -u "$cp_utils_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: cp utils imports host-facing $symbol" >&2
        exit 1
    fi
done

mkdir_source=upstream/netbsd/bin/mkdir/mkdir.c
mkdir_object=$build_path/netbsd_mkdir.o
mkdir_hash=a3abf691d386bd2b8a23483e0dd7abb314403b281031d3b58cd3abec4e0926d8

if [[ $(sha256sum "$mkdir_source" | awk '{print $1}') != "$mkdir_hash" ]] ||
   ! matches "$mkdir_hash" "$provenance_file"; then
    echo 'FAIL: mkdir source/provenance pin differs' >&2
    exit 1
fi
if ! nm "$mkdir_object" | matches '[[:space:]]T[[:space:]]+cb_mkdir_main$' ||
   nm "$mkdir_object" | matches '[[:space:]]T[[:space:]]+main$'; then
    echo 'FAIL: mkdir entry point is not privately renamed' >&2
    exit 1
fi
for symbol in chmod err errno_location exit fprintf free getmode getopt \
        getopt_state_location getprogname mkdir setlocale setmode setprogname \
        stat stderr_stream strcspn strspn umask warn; do
    if nm -u "$mkdir_object" | matches "[[:space:]]U[[:space:]]+${symbol}$"; then
        echo "FAIL: mkdir imports host-facing $symbol" >&2
        exit 1
    fi
done

echo 'pinned unmodified NetBSD source boundary passed'
