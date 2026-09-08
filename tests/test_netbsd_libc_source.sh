#!/usr/bin/env bash
set -euo pipefail

# Drain producer output: early exit from rg -q can SIGPIPE nm/ar under
# pipefail even after finding a valid symbol. Preserve actual producer errors.
matches() {
    rg "$@" >/dev/null
}

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
build_path=${BUILD_PATH:-build}
source_file=upstream/netbsd/common/lib/libc/string/strlen.c
object_file=$build_path/netbsd_strlen.o
archive_file=$build_path/libcannedbsd.a
expected_hash=08969942df6b9b53bb3500e39ec47f514b876809202383088ac9d36e007d64e1
expected_revision=b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c
provenance_file=UPSTREAM.md
strcmp_source=upstream/netbsd/common/lib/libc/string/strcmp.c
strcmp_object=$build_path/netbsd_strcmp.o
strcmp_hash=f06298e20a2c02e9fbe11aeb06123d8b2ad6c8d5a9a04ad68fdae2aa142524f6
strcpy_source=upstream/netbsd/common/lib/libc/string/strcpy.c
strcpy_object=$build_path/netbsd_strcpy.o
strcpy_hash=36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad
memcpy_source=upstream/netbsd/common/lib/libc/string/memcpy.c
bcopy_source=upstream/netbsd/common/lib/libc/string/bcopy.c
memcpy_object=$build_path/netbsd_memcpy.o
memcpy_hash=27954650049d23535119c13fec0d333929e6ad17bb80d3c5e33ac9938f57f2a4
bcopy_hash=915b194678b2855a522755dad71ae4fb4f366d3f0518fd089bc6c2cb35722c44
memmove_source=upstream/netbsd/common/lib/libc/string/memmove.c
memmove_object=$build_path/netbsd_memmove.o
memmove_hash=a28ca02301f0800d67b1d8b35e1d1021b7600deb6b7e82f4179023aa22f7756b
memcmp_source=upstream/netbsd/common/lib/libc/string/memcmp.c
memcmp_object=$build_path/netbsd_memcmp.o
memcmp_hash=a926ba117d7a044631da27bc301769607072bdf42995e4d49dbc00139643d5ce
strchr_source=upstream/netbsd/common/lib/libc/string/strchr.c
strchr_object=$build_path/netbsd_strchr.o
strchr_hash=ebe71501c3aa96b35445642eeb72ab6c73f0fa561ce83b9f78d4c0e06c155cb9
dirname_source=upstream/netbsd/lib/libc/gen/dirname.c
dirname_object=$build_path/netbsd_dirname.o
dirname_hash=05ad1f66a7a5a4ceee33fe767a3410c60aa76e4ed670b5d57ab9198a0a2a892b
basename_source=upstream/netbsd/lib/libc/gen/basename.c
basename_object=$build_path/netbsd_basename.o
basename_hash=f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87

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
        ! matches "$expected_revision" "$provenance_file" ||
        ! matches "$expected_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strlen provenance is absent or does not match the pin' >&2
    exit 1
fi
if matches '\bassert[[:space:]]*\(' "$source_file"; then
    echo 'FAIL: the import-only assert shim is unsafe for this source' >&2
    exit 1
fi
if [[ ! -f $object_file ]] ||
        ! nm "$object_file" | matches '[[:space:]]T[[:space:]]+cb_libc_strlen$'; then
    echo 'FAIL: NetBSD strlen was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$object_file" | matches '[[:space:]]U[[:space:]]+strlen$'; then
    echo 'FAIL: NetBSD strlen object imports host strlen' >&2
    exit 1
fi
if [[ ! -f $archive_file ]] ||
        ! ar t "$archive_file" | matches '^netbsd_strlen\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strlen' >&2
    exit 1
fi
if ! nm -u "$build_path/cb_libc.o" |
        matches '[[:space:]]U[[:space:]]+cb_libc_strlen$'; then
    echo 'FAIL: cannedBSD puts does not use NetBSD strlen' >&2
    exit 1
fi
if [[ ! -f $strcmp_source ]]; then
    echo "FAIL: pinned NetBSD strcmp source is missing: $strcmp_source" >&2
    exit 1
fi
actual_strcmp_hash=$(sha256sum "$strcmp_source" | awk '{print $1}')
if [[ $actual_strcmp_hash != "$strcmp_hash" ]]; then
    printf 'FAIL: NetBSD strcmp source changed: expected %s, found %s\n' \
        "$strcmp_hash" "$actual_strcmp_hash" >&2
    exit 1
fi
if ! matches "$strcmp_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strcmp provenance does not match the pin' >&2
    exit 1
fi
if matches '\bassert[[:space:]]*\(' "$strcmp_source"; then
    echo 'FAIL: the import-only assert shim is unsafe for strcmp' >&2
    exit 1
fi
if [[ ! -f $strcmp_object ]] ||
        ! nm "$strcmp_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_strcmp$'; then
    echo 'FAIL: NetBSD strcmp was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$strcmp_object" | matches '[[:space:]]U[[:space:]]+strcmp$'; then
    echo 'FAIL: NetBSD strcmp object imports host strcmp' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_strcmp\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strcmp' >&2
    exit 1
fi
if [[ ! -f $strcpy_source ]]; then
    echo "FAIL: pinned NetBSD strcpy source is missing: $strcpy_source" >&2
    exit 1
fi
actual_strcpy_hash=$(sha256sum "$strcpy_source" | awk '{print $1}')
if [[ $actual_strcpy_hash != "$strcpy_hash" ]]; then
    printf 'FAIL: NetBSD strcpy source changed: expected %s, found %s\n' \
        "$strcpy_hash" "$actual_strcpy_hash" >&2
    exit 1
fi
if ! matches "$strcpy_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strcpy provenance does not match the pin' >&2
    exit 1
fi
if matches '\bassert[[:space:]]*\(' "$strcpy_source"; then
    echo 'FAIL: the import-only assert shim is unsafe for strcpy' >&2
    exit 1
fi
if [[ ! -f $strcpy_object ]] ||
        ! nm "$strcpy_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_strcpy$'; then
    echo 'FAIL: NetBSD strcpy was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$strcpy_object" | matches '[[:space:]]U[[:space:]]+strcpy$'; then
    echo 'FAIL: NetBSD strcpy object imports host strcpy' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_strcpy\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strcpy' >&2
    exit 1
fi
if [[ ! -f $memcpy_source || ! -f $bcopy_source ]]; then
    echo 'FAIL: pinned NetBSD memcpy or bcopy source is missing' >&2
    exit 1
fi
actual_memcpy_hash=$(sha256sum "$memcpy_source" | awk '{print $1}')
actual_bcopy_hash=$(sha256sum "$bcopy_source" | awk '{print $1}')
if [[ $actual_memcpy_hash != "$memcpy_hash" ||
      $actual_bcopy_hash != "$bcopy_hash" ]]; then
    echo 'FAIL: NetBSD memcpy source set changed' >&2
    exit 1
fi
if ! matches "$memcpy_hash" "$provenance_file" ||
        ! matches "$bcopy_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD memcpy provenance does not match the source set' >&2
    exit 1
fi
if [[ ! -f $memcpy_object ]] ||
        ! nm "$memcpy_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_memcpy$'; then
    echo 'FAIL: NetBSD memcpy was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$memcpy_object" | matches '[[:space:]]U[[:space:]]+memcpy$'; then
    echo 'FAIL: NetBSD memcpy object imports host memcpy' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_memcpy\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD memcpy' >&2
    exit 1
fi
if [[ ! -f $memmove_source ]]; then
    echo 'FAIL: pinned NetBSD memmove source is missing' >&2
    exit 1
fi
actual_memmove_hash=$(sha256sum "$memmove_source" | awk '{print $1}')
if [[ $actual_memmove_hash != "$memmove_hash" ]]; then
    echo 'FAIL: NetBSD memmove source changed' >&2
    exit 1
fi
if ! matches "$memmove_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD memmove provenance does not match the pin' >&2
    exit 1
fi
if [[ ! -f $memmove_object ]] ||
        ! nm "$memmove_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_memmove$'; then
    echo 'FAIL: NetBSD memmove was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$memmove_object" | matches '[[:space:]]U[[:space:]]+memmove$'; then
    echo 'FAIL: NetBSD memmove object imports host memmove' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_memmove\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD memmove' >&2
    exit 1
fi
if [[ ! -f $memcmp_source ]] ||
        [[ $(sha256sum "$memcmp_source" | awk '{print $1}') != "$memcmp_hash" ]]; then
    echo 'FAIL: pinned NetBSD memcmp source is missing or changed' >&2
    exit 1
fi
if ! matches "$memcmp_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD memcmp provenance does not match the pin' >&2
    exit 1
fi
if matches '\bassert[[:space:]]*\(' "$memcmp_source"; then
    echo 'FAIL: the import-only assert shim is unsafe for memcmp' >&2
    exit 1
fi
if [[ ! -f $memcmp_object ]] ||
        ! nm "$memcmp_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_memcmp$'; then
    echo 'FAIL: NetBSD memcmp was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$memcmp_object" | matches '[[:space:]]U[[:space:]]+memcmp$'; then
    echo 'FAIL: NetBSD memcmp object imports host memcmp' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_memcmp\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD memcmp' >&2
    exit 1
fi
if [[ ! -f $strchr_source ]] ||
        [[ $(sha256sum "$strchr_source" | awk '{print $1}') != "$strchr_hash" ]]; then
    echo 'FAIL: pinned NetBSD strchr source is missing or changed' >&2
    exit 1
fi
if ! matches "$strchr_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strchr provenance does not match the pin' >&2
    exit 1
fi
if matches '\bassert[[:space:]]*\(' "$strchr_source"; then
    echo 'FAIL: the import-only assert shim is unsafe for strchr' >&2
    exit 1
fi
if [[ ! -f $strchr_object ]] ||
        ! nm "$strchr_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_strchr$'; then
    echo 'FAIL: NetBSD strchr was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$strchr_object" | matches '[[:space:]]U[[:space:]]+strchr$'; then
    echo 'FAIL: NetBSD strchr object imports host strchr' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_strchr\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strchr' >&2
    exit 1
fi
if [[ ! -f $dirname_source ]] ||
        [[ $(sha256sum "$dirname_source" | awk '{print $1}') != "$dirname_hash" ]]; then
    echo 'FAIL: pinned NetBSD dirname source is missing or changed' >&2
    exit 1
fi
if ! matches "$dirname_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD dirname provenance does not match the pin' >&2
    exit 1
fi
if [[ ! -f $dirname_object ]] ||
        ! nm "$dirname_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_dirname_upstream$'; then
    echo 'FAIL: NetBSD dirname was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$dirname_object" | matches '[[:space:]]U[[:space:]]+dirname$'; then
    echo 'FAIL: NetBSD dirname object imports host dirname' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_dirname\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD dirname' >&2
    exit 1
fi
if ! nm -u "$build_path/cb_libc.o" |
        matches '[[:space:]]U[[:space:]]+cb_libc_dirname_upstream$'; then
    echo 'FAIL: cannedBSD dirname veneer does not use the imported dirname' >&2
    exit 1
fi
if [[ ! -f $basename_source ]] ||
        [[ $(sha256sum "$basename_source" | awk '{print $1}') != "$basename_hash" ]]; then
    echo 'FAIL: pinned NetBSD basename source is missing or changed' >&2
    exit 1
fi
if ! matches "$basename_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD basename provenance does not match the pin' >&2
    exit 1
fi
if [[ ! -f $basename_object ]] ||
        ! nm "$basename_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_basename_upstream$'; then
    echo 'FAIL: NetBSD basename was not compiled under its private link name' >&2
    exit 1
fi
if nm -u "$basename_object" | matches '[[:space:]]U[[:space:]]+basename$'; then
    echo 'FAIL: NetBSD basename object imports host basename' >&2
    exit 1
fi
if ! ar t "$archive_file" | matches '^netbsd_basename\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD basename' >&2
    exit 1
fi
if ! nm -u "$build_path/cb_libc.o" |
        matches '[[:space:]]U[[:space:]]+cb_libc_basename_upstream$'; then
    echo 'FAIL: cannedBSD basename veneer does not use the imported basename' >&2
    exit 1
fi

strtoimax_source=upstream/netbsd/common/lib/libc/stdlib/strtoimax.c
strtoimax_object=$build_path/netbsd_strtoimax.o
strtoimax_hash=c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5
strtol_h_source=upstream/netbsd/common/lib/libc/stdlib/_strtol.h
strtol_h_hash=f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c

if [[ ! -f $strtoimax_source ]] ||
        [[ $(sha256sum "$strtoimax_source" | awk '{print $1}') != "$strtoimax_hash" ]]; then
    echo 'FAIL: pinned NetBSD strtoimax source is missing or changed' >&2
    exit 1
fi
if [[ ! -f $strtol_h_source ]] ||
        [[ $(sha256sum "$strtol_h_source" | awk '{print $1}') != "$strtol_h_hash" ]]; then
    echo 'FAIL: pinned NetBSD _strtol.h source is missing or changed' >&2
    exit 1
fi
if ! matches "$strtoimax_hash" "$provenance_file" ||
        ! matches "$strtol_h_hash" "$provenance_file"; then
    echo 'FAIL: NetBSD strtoimax/_strtol.h provenance does not match the pin' >&2
    exit 1
fi
if [[ ! -f $strtoimax_object ]] ||
        ! nm "$strtoimax_object" |
            matches '[[:space:]]T[[:space:]]+cb_libc_strtoimax$'; then
    echo 'FAIL: NetBSD strtoimax was not compiled under its private link name' >&2
    exit 1
fi
for host_symbol in strtoimax isspace isdigit; do
    if nm -u "$strtoimax_object" |
            matches "[[:space:]]U[[:space:]]+${host_symbol}\$"; then
        printf 'FAIL: NetBSD strtoimax object imports host-facing %s\n' \
            "$host_symbol" >&2
        exit 1
    fi
done
for private_symbol in cb_libc_isspace cb_libc_errno_location; do
    if ! nm -u "$strtoimax_object" |
            matches "[[:space:]]U[[:space:]]+${private_symbol}\$"; then
        printf 'FAIL: NetBSD strtoimax object does not import %s\n' \
            "$private_symbol" >&2
        exit 1
    fi
done
if ! ar t "$archive_file" | matches '^netbsd_strtoimax\.o$'; then
    echo 'FAIL: libcannedbsd.a does not contain NetBSD strtoimax' >&2
    exit 1
fi

echo 'pinned unmodified NetBSD libc source boundary passed'
