# CONV-01: pinned strtoimax and its bounded C-locale prerequisites

- Status: implementation, per the reviewed and Retro68-measured
  `notes/iterations/CONV-01-design.md` (`fe51f3d`).
- Base: `origin/work/warn-stdin-integration` at `69ee9ff` (fresh worktree,
  `work/CONV-01`).
- Scope, exactly as designed: unchanged `strtoimax.c`/`_strtol.h`,
  `cb_libc_isdigit`/`cb_libc_isspace`, `ERANGE` and its `strerror` mapping,
  the `_DIAGASSERT`/`nbtool_config.h` import-only shims. `strcpy` is
  explicitly excluded (tracked separately as `STRCPY-01`).

## What was actually built

- `upstream/netbsd/common/lib/libc/stdlib/strtoimax.c` and `_strtol.h`,
  vendored unchanged. Hashes re-verified directly against the design's
  own pinned values (not merely copied):
  `strtoimax.c` = `c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5`,
  `_strtol.h` = `f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c`.
  `UPSTREAM.md` records both, with their two distinct licenses.
- `compat/netbsd/include/assert.h` (existing, shared shim): added
  `#define _DIAGASSERT(e) ((void)0)`, per the design's corrected
  include-order fix -- not a new `libc/include/assert.h`.
- `compat/netbsd/include/nbtool_config.h` (new, empty): satisfies
  `strtoimax.c`'s own `#if HAVE_NBTOOL_CONFIG_H #include
  "nbtool_config.h" #endif` line and nothing else.
- `libc/include/ctype.h` (new): `#define isdigit cb_libc_isdigit`,
  `#define isspace cb_libc_isspace`.
- `libc/include/inttypes.h` (new): borrows `intmax_t`/`INTMAX_MAX`/
  `INTMAX_MIN` from the host's own `<stdint.h>` (pure compile-time
  types, this project's existing fundamental-header exemption), declares
  and maps `strtoimax` to the private veneer -- the actual host-leak
  risk this header exists to close.
- `libc/include/errno.h`: `#define ERANGE CB_ERANGE`. `include/
  cannedbsd/abi.h`: `CB_ERANGE = 34` added to `enum cb_error` (a plain
  named-constant addition, not a struct field -- no ABI-tail concern).
  `src/core.c`'s `api_strerror` gets its own `case CB_ERANGE: return
  "result too large";`.
- `libc/cb_libc.c`: `cb_libc_isdigit`/`cb_libc_isspace`, two small,
  stateless, pure functions -- no `bound_api` use at all, since neither
  needs any task-owned state.
- Makefile: `NETBSD_STRTOIMAX_OBJECT`, compiled with
  `-Icompat/netbsd/include -Ilibc/include -DHAVE_NBTOOL_CONFIG_H=1
  -Dstrtoimax=cb_libc_strtoimax`, archived into `libcannedbsd.a`.
- `tests/libc_strtoimax_probe.c` + `_module.c`: a native probe
  (`libcstrtoimaxprobe`) covering the design's full matrix, registered
  under a new, minimal `FIXTURE_CONV` (not `FIXTURE_FULL`, which is
  already at 63/64 after `ECHO-01`'s own scoping fix -- adding this
  probe there would have overflowed it again).
- `tests/test_netbsd_libc_source.sh`: a permanent source-boundary
  section for `strtoimax`/`_strtol.h`, mirroring the existing
  `dirname`/`basename` sections exactly -- hash/provenance pins, private
  symbol present, no host `strtoimax`/`isspace`/`isdigit` import, private
  `cb_libc_isspace`/`cb_libc_errno_location` imports present, archive
  membership.

## Honest verification: what was actually executed, and where

Docker remains down on this design-worktree host, and this project's
runtime needs `ucontext(3)`, deprecated/removed on this host's macOS SDK
-- the same constraint every prior iteration this session has recorded.
Unlike those iterations, this one did not stop at `clang -fsyntax-only`:
the coordinator's earlier direction to reach the CI runner host over SSH
for the Retro68 measurement applies equally well here, since that same
host also has a real, working native Linux toolchain directly on its
`PATH` (not just inside the pinned Retro68 container). The whole
worktree was synced there and **actually built and run** -- genuine
execution evidence, not just compile evidence:

- `make build/test_core` succeeded; `./build/test_core` printed `all
  core tests passed` and exited `0`, including the new
  `libcstrtoimaxprobe` case (28 assertions from the design's matrix,
  covering base `0`/`2`/`10`/`16`/`36`, invalid bases, signs, leading
  whitespace, `NULL` endptr, `INTMAX_MAX`/`INTMAX_MIN` overflow and
  underflow with exact `endptr`/`errno` transitions, continued `endptr`
  scanning past every digit after overflow latches, the exhaustive
  257-value `isdigit`/`isspace` sweep, and the `ERANGE` `strerror`
  mapping).
- `make test` (the full target, not just the test binary) also passed
  end to end on that host: `architecture boundary checks passed`,
  `pinned unmodified NetBSD source boundary passed`, `pinned unmodified
  NetBSD libc source boundary passed` (the new `strtoimax`/`_strtol.h`
  section included), `echo behavioral matrix passed`, and every other
  existing behavioral suite, with a final exit status of `0`.
- `tests/test_publication.sh` passed locally against this actual git
  worktree (hygiene checks, not touched by this task's changes).

### Genuine red, and a separate source-boundary negative control -- not conflated

Per the backlog's own explicit distinction ("missing symbols are
separate source-boundary evidence, not behavior red"):

**Behavioral red/green**: temporarily broke `cb_libc_isdigit` (changed
`>= '0'` to `> '0'`) on the same built copy, rebuilt, and reran.
`./build/test_core` failed exactly as expected: `command:
libcstrtoimaxprobe`, `expected status/output: 0 <>`, `actual
status/output: 27 <>` (`check_ctype`'s own failure code), overall exit
status `1`. Restored the original line, rebuilt, reran: `all core tests
passed`, exit `0` again. This is genuine, actually-observed red then
green for a real behavioral defect, not a hypothetical.

**Source-boundary negative control, kept separate and correctly
labeled**: removed the `-Dstrtoimax=cb_libc_strtoimax` Makefile flag and
rebuilt. It still linked and passed -- because `libc/include/
inttypes.h`'s own `#define strtoimax cb_libc_strtoimax` mapping already
renames the definition, since `strtoimax.c` itself `#include`s
`<inttypes.h>` (confirmed directly with `nm`: the object still exports
`cb_libc_strtoimax`, not a bare `strtoimax`, with the flag removed).
This is a real, useful finding -- the Makefile flag is redundant with
the header's own mapping, not required -- and is recorded honestly as
such rather than reported as a "negative control" that actually failed
when it did not. The Makefile flag is kept anyway, for the same
explicit, self-documenting-rename style every other imported source in
this project already uses. The permanent `test_netbsd_libc_source.sh`
section added above (hash pin, private-symbol presence, host-symbol
absence) is this task's actual durable source-boundary evidence, run
automatically on every future `make test`/CI, rather than a one-off
manual demonstration.

### A pre-existing environment mismatch found, and explicitly not attributed to this task

Attempting `make CC=clang BUILD_VARIANT=sanitize ... test` directly over
SSH on that same host failed at `test_dirname_behavior.sh`'s sanitizer-
report check -- but on `dirname`, a command this task does not touch at
all. The actual cause: this host's system `clang`/compiler-rt emits an
*additional* `ASan doesn't fully support makecontext/swapcontext
functions...` warning line ahead of the three lines
`check_sanitizer_reports` already expects and tolerates, which its exact
`awk` pattern (matching exactly 3 lines, in order) rejects as a fourth,
unrecognized line. This is a toolchain-version artifact of this
particular direct-SSH shell environment, not of the pinned Woodpecker
`ci` image/pipeline this branch's own commits have already passed
`make sanitize` on repeatedly this session (`BASENAME-01`, `ECHO-01`,
and this design's own prior commits) -- recorded honestly as an observed
mismatch, not silently worked around, and not treated as evidence this
task's own change is unsound. Woodpecker's actual `ci` check (below)
is the authoritative sanitize-build gate, exactly per this session's
established practice.

## Constraints preserved

- `CB_MAX_PROGRAMS` untouched; `FIXTURE_CONV` is a new, minimal scoped
  fixture (one probe program), not a `FIXTURE_FULL` addition.
- No runtime ABI change: none of `strtoimax`/`isdigit`/`isspace`/
  `ERANGE` touch `cb_api_v1` or any versioned struct.
- No host symbols: verified by the new permanent
  `test_netbsd_libc_source.sh` section, not merely asserted.
- No new compiler helpers: the design's own Retro68 measurement already
  confirmed `libgcc` supplies `__muldi3`/`__divdi3` automatically; this
  task adds no arithmetic beyond what the unchanged imported source
  already performs.
- `strcpy` untouched, per the coordinator's explicit exclusion.

## Review round: three blockers, one of them a real, previously-hidden bug

Independent review of `2dc78c5` found three real gaps before merge.

**1. `platform/mac68k` was never touched.** The Retro68 side excluded
`strtoimax` entirely. Fixed: `platform/mac68k/CMakeLists.txt` gets a
`cb_strtoimax` import target (`compat/netbsd/include` before
`libc/include`, `HAVE_NBTOOL_CONFIG_H=1`, `strtoimax=cb_libc_strtoimax`
-- the same private rename as the Linux build) and a `cb_strtoimax_probe`
object+module, both wired into `add_application`/`target_sources` exactly
like `cb_basename`/`cb_basename_probe`. `platform/mac68k/main.c` gets the
matching `cb_kernel_register(kernel, &cb_strtoimax_probe_program)` in its
checked chain. `platform/mac68k/acceptance_cases.def` gets one new case
(`libcstrtoimaxprobe`), moving the guest-run total from 54 to 55 (the
coordinator's own separate integration will move it further, to 60, when
other pending work lands -- not this task's concern). **Verified with a
real build, not just written and hoped**: synced this branch to the CI
runner host and ran the exact `platform/mac68k/ci-build.sh` sequence
against the pinned Retro68 image over Docker there --
`cmake -S platform/mac68k -B build-mac68k -DCMAKE_TOOLCHAIN_FILE=...` and
`cmake --build build-mac68k -j2` both exited `0`, `build-mac68k/
CannedBSD.bin` and `.rsrc/CannedBSD.APPL` were produced, and
`check_code_resources.py` confirmed the single-CODE-segment invariant --
the exact same checks `mac68k`'s own Woodpecker workflow runs.

**2. The `ERANGE` `strerror` test accepted a silent regression.** The
first version of `check_strerror` only asserted the message was
non-empty and different from `EINVAL`'s -- a disposable removal of the
`CB_ERANGE` case from `api_strerror` falls through to its `"unknown
error"` default, which is *still* non-empty and *still* different from
`EINVAL`'s message, so that version would not have caught it. Fixed to
assert the exact string `"result too large"` and to seed `errno` with
the sentinel around the call, confirming `strerror` itself doesn't
disturb it. **Verified with an actual disposable removal, not just
reasoned about**: deleted the `case CB_ERANGE:` line from `src/core.c`
on the built copy, rebuilt, and reran -- `./build/test_core` now fails
exactly at `libcstrtoimaxprobe`, exit code `28` (`check_strerror`'s own
failure code), overall exit status `1`. Restored the line, rebuilt,
reran -- green again, exit `0`.

**3. The source-boundary check only covered the imported object, not
ordinary calling code.** `tests/test_netbsd_libc_source.sh`'s new
section (from the previous commit) checks `netbsd_strtoimax.o` -- the
*upstream* object -- for host-symbol absence and private-symbol
presence, but nothing checked that *ordinary code calling* `strtoimax`/
`isdigit`/`isspace` (like this task's own probe) actually resolves
against the private veneer rather than a host declaration reached
through some other header path. Added a dedicated section to
`tests/test_libc_source.sh` (the file that already does exactly this
for `memcpy`/`warn`/`getopt`/etc from ordinary source), covering
`tests/libc_strtoimax_probe.c`/`strtoimaxprobe_command.o`: source text
has no `cb_`-prefixed names, and the compiled object's undefined
symbols include `cb_libc_strtoimax`/`cb_libc_isdigit`/
`cb_libc_isspace`/`cb_libc_errno_location`/`cb_libc_strcmp`/
`cb_libc_strerror` (all six, matching exactly what `nm -u` on that
object actually shows) and none of their bare host-facing names.

**A fourth, independent bug found while fixing these** (not in the
original review, found because actually building and running things
surfaced it): adding `platform/mac68k/acceptance_cases.def`'s new case
also feeds `tests/test_core.c`'s `test_mac_acceptance()`, which replays
that same `.def` file on the *Linux* side under `FIXTURE_MAC` --
because it is literally `#include`d with `CB_MAC_CASE` mapped to
`run_case(..., FIXTURE_MAC)`, as a native mirror of the guest run. That
fixture's own registration list, `register_mac_probes`, did not include
`cb_strtoimax_probe_program`, so the *native* run failed with `127:
libcstrtoimaxprobe: no such file or directory` despite `FIXTURE_CONV`'s
own separate registration being completely correct. This was invisible
to `-fsyntax-only` and only surfaced by actually running `make test`;
diagnosed by adding a temporary `fprintf` to confirm `register_conv_
probes` was never reached for this call path (it runs under `FIXTURE_
MAC`, not `FIXTURE_CONV`, for this specific case), then fixed by adding
the same registration to `register_mac_probes`, mirroring
`cb_basename_probe_program`'s own presence there. Removed the debug
print before committing. Reran the full `make test` clean from scratch
on the CI runner host afterward: exit `0`, no failures.

All of the above -- the Mac CMake/registration/acceptance-case fix, the
`ERANGE` exact-string fix with its own real remove/rebuild/restore
cycle, the new `test_libc_source.sh` section, and the `register_mac_
probes` fix -- were built and run on the pinned Linux CI toolchain
directly on the CI runner host (not this design-worktree host's
mismatched local `clang`), per the coordinator's explicit instruction.

## Guest acceptance: pending, not waived

This is a runtime/libc behavior change. Per the backlog's explicit
correction from `CONV-01-design`'s own review, guest acceptance is
recorded here as **pending** -- not "not required" -- until the
coordinator runs the exact built `mac68k` artifact in Basilisk II. That
remains the coordinator's own action, not claimed here.

## Coordinator acceptance, 2026-09-08

Integrated with the other reviewed head prerequisites at
`ff08dd5c22f6c74de0bed5afce7a9798d666b84b`. Exact Woodpecker #301 passed all
three workflows. Fresh guest `run-3c7aibdj` passed all 62 records; the full
transcript and screenshot were inspected. Cold automated cycle: 22.43 seconds,
including normal app/guest shutdown, verified closed disks and slot release.
Artifact archive SHA256:
`f863c4c70f2518c9bf4f23f6aab33f147eb4cc62b50bc80f01e4ec22d8770e37`.
Merged to main only after these gates. The first CONV integration attempt
failed protocol tests because its manifest contained an extra blank line;
removing that line restored all 18 protocol tests before this accepted build.
