# STRCASECMP-C-01 — C/POSIX ASCII case comparison

Base: reviewed COLLATE-C-01 candidate
`fb0b543fb2b823155778c1a8fc0fd58053752815`. Branch
`work/STRCASECMP-C-01`, sibling worktree `bsdinacan-STRCASECMP-C-01`.
The NEXT-UTIL-04 pinned comm measurement establishes the missing interface;
this prerequisite does not import comm or claim it executes.

Hypothesis: unsigned-byte comparison after folding only ASCII A through Z
provides the existing C/POSIX-only profile without host locale or task state.
The pure private helper preserves incoming errno. New private strings.h maps
the ordinary name; string.h also exposes the BSD surface needed by unchanged
comm. No ABI fields, allocation, locale expansion or strncasecmp are added.

The baseline exact base passed `make clean test LDLIBS=-lucontext` in a fresh,
network-disabled `tribblix-woodpecker-agent:3.18.0` container on biggie.
Before implementation, the new ordinary-source case header was added to the
existing locale probe. This command then exited 1:

```sh
clang -nostdinc -ffreestanding -isystem "$(clang -print-resource-dir)/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic \
  -fsyntax-only tests/libc_locale_probe.c
```

Exact diagnostic: `tests/libc_strcasecmp_cases.h:10:50: error: use of
undeclared identifier 'strcasecmp'`. After implementation strict compilation
passed. Tests cover mixed-case equality, prefixes, punctuation, first-NUL
termination, all 256-by-256 byte pairs, unchanged high bytes, errno and rejected
locale changes. Native checks also call the helper before kernel creation and
after destruction. Both private header paths expose the ordinary function.

An initial focused run exposed an incorrect punctuation expectation in the
test: '[' sorts before folded 'z'. The expected sign was corrected, without
changing the helper, followed by a clean focused rebuild. This test correction
is separate from the genuine missing-interface red above.

The clean focused Linux run passed `make -j4 build/test_core
check-acceptance-output LDLIBS=-lucontext`, followed by `build/test_core --locale`
and `build/test_core --mac-acceptance`, in the same pinned container profile.

Host protocol and build-parity checks pass. The suite retains the earlier 70
records and adds `libclocaleprobe casecmp`: 71 records, 1771 bytes including
the terminator, within the existing result capacity. Twenty protocol tests
pass, including rejection of both earlier 69- and 70-record transcripts.

Focused runtime and final full-gate results are reported with the selected
commit in the coordinator handoff. Exact Woodpecker ci/mac68k/mac-automation,
fresh Mac guest execution and Solaris qualification remain separate gates.
No guest artifacts staged, reserved emulator operated, merge or broad locale
acceptance claimed.

Shared symbols touched: new cb_libc_strcasecmp and its ordinary mapping;
cb_libc_strcoll and existing libc/ABI behavior are retained. Shared-file edits
were coordinated explicitly before implementation. Owned documentation changes
are this note and LIBC.md; queue/current-state rollups remain coordinator-owned.
