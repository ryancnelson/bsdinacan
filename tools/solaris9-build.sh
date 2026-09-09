#!/bin/ksh
# Solaris 9 SPARC (sun4m) native build and shared runtime gate.
#
# Requires the Sunfreeware GCC 3.4.6, GNU make 3.81, and libiconv 1.8
# packages documented in SOLARIS9.md, already installed in the guest,
# and librt for gethrtime()/POSIX clocks.
#
# Deliberately does NOT invoke `gmake test`: that target depends on
# check-architecture (uses `rg`, not assumed present in this guest) and
# on tests/test_one_process.sh (reads Linux's /proc, meaningless here).
# Those, and the source-provenance checks (test_libc_source.sh etc.),
# are host-side checks already covered by the Linux `make ci` gate; see
# SOLARIS9.md. This script instead builds and runs only the shared,
# portable runtime surface directly: the core test binary, the
# launcher, and the same acceptance-output assertions the historical
# reference (work/SOLARIS-01-reference, a28f9ed) used as its own
# "test-runtime" gate.
#
# The worker reports a fresh native PASS on 698541f. Coordinator
# verification of the source archive, full transcript, and successful
# toolchain environment remains pending; main integration is not yet
# accepted. See SOLARIS9.md and notes/iterations/SOLARIS-01.md.
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

CC=${CC:-gcc}
MAKE=${MAKE:-make}

# Deliberately NOT -Ilibc/include here: that directory holds private
# NetBSD-import veneer headers (its own string.h/stdio.h/stdint.h/etc.)
# that the Makefile already adds per-command-object, only for sources
# built against that veneer. Adding it to a global CPPFLAGS override
# would redirect ordinary sources' #include <string.h> and friends
# through that private veneer instead of the guest's real system
# headers -- affecting every compilation unit, not just the intended
# few. Only add -Icompat/solaris9/include, which holds nothing but the
# stdint.h/SIZE_MAX adapter and has no name collisions with the
# per-command veneer.
CPPFLAGS="-D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9 -Icompat/solaris9/include -Iinclude -Isrc"
# gnu99, not strict c99: the historical reference recorded that GCC
# 3.4.6 needs GNU C99 mode to expose the original Solaris headers' own
# 64-bit integer types; strict -std=c99 failed there. -pedantic (GCC
# 3.x spelling), not -Wpedantic. -Wno-unknown-pragmas: found via a real
# guest build, not assumed -- the guest's own /usr/include/inttypes.h
# uses a Sun-specific `#pragma ident "..."` that GCC 3.4.6 does not
# recognize; under -Werror that becomes a hard build failure from the
# *vendor's own system header*, not from anything in this project's
# source. This tolerates only that one class of vendor-header warning;
# every warning this project's own code can trigger stays fatal.
CFLAGS="-std=gnu99 -Wall -Wextra -Werror -pedantic -Wno-unknown-pragmas -g -O2"
LDLIBS="-lrt"

export CC CPPFLAGS CFLAGS LDLIBS

"$CC" --version
"$MAKE" --version

"$MAKE" SHELL=/bin/ksh clean
# HEAD_STACKFLAGS=: GCC 3.4.6 predates -fstack-usage (GCC 4.6+); this
# guest build has no .su stack-usage report for netbsd_head.o, and does
# not claim one. Every other build (Linux, Retro68 Mac68k) is
# unaffected and keeps the default -fstack-usage.
"$MAKE" SHELL=/bin/ksh HEAD_STACKFLAGS= build/bsdinacan build/test_core

./build/test_core
PROGRAM_PATH='build/bsdinacan' /bin/ksh tests/test_launcher.sh

output=$(./build/bsdinacan -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result')
test "$output" = HELLO || { print -r -- "acceptance output: <$output>"; exit 1; }
output=$(./build/bsdinacan -c 'false; echo $?')
test "$output" = 1 || { print -r -- "exit-status acceptance output: <$output>"; exit 1; }
output=$(./build/bsdinacan -c 'echo -n hello | wc -c')
test "$output" = 5 || { print -r -- "libc acceptance output: <$output>"; exit 1; }

file build/bsdinacan
print -r -- 'SOLARIS9_CANNEDBSD_TEST=PASS'
