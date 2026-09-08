# LOCALE-01: bounded C-only setlocale

- Status: implemented; exact feature CI and coordinator guest acceptance pending.
- Base: `592ae410770ae10dbe8c9707b64ab6a1daf220a9`, freshly fetched main.
- Branch: `work/LOCALE-01`; worktree: `../bsdinacan-LOCALE-01`.
- Assigned milestone: unchanged pinned NetBSD `dirname` utility. Claude owns the
  separate `dirname(3)` prerequisite; this task supplies only measured locale
  startup. The coordinator approved the following bounded contract before the
  implementation body was written.

## Measured need and contract

The pinned `usr.bin/dirname/dirname.c`, revision
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, SHA256
`839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`, includes
`locale.h` and calls `setlocale(LC_ALL, "")`, ignoring the return value. No other
locale function or category argument is required by that source. It was inspected
from the coordinator's existing source audit, not imported or modified here.

`locale.h` therefore exposes only `LC_ALL` and private-link `setlocale`:

- Initial effective locale is C. `setlocale(LC_ALL, NULL)` queries it and returns
  `"C"`. Explicit `"C"` and `"POSIX"` succeed and normalize to `"C"`.
- An empty locale request selects from the **current cannedBSD task's**
  environment. A nonempty `LC_ALL` overrides every category and `LANG`.
  Otherwise, each of `LC_COLLATE`, `LC_CTYPE`, `LC_MONETARY`, `LC_NUMERIC`,
  `LC_TIME`, and `LC_MESSAGES` selects its nonempty variable, then nonempty
  `LANG`, then C. Empty values behave as unset.
- Every selected category must resolve to exactly C or POSIX. Any unsupported
  or mixed non-C selection returns NULL. `C.UTF-8`, arbitrary names, case
  variants and composite locale strings are not silently accepted.
- Unsupported numeric category arguments return NULL. Consulting category
  environment variables does not advertise category-specific API support.
- Success and rejection preserve errno. Rejection leaves the effective C
  profile and prior returned values unchanged. No new error constant exists.
- The result is a borrowed immutable string; callers must not write through it.
  It does not allocate and remains valid across calls and task interleaving.

This is task-environment-aware, not mutable per-task locale state. Every success
is the same C profile, so no task fields, cleanup, scheduling, or ABI additions
are needed. Spawn/exec already supply the current task environment. The existing
API `getenv` is in the old mandatory-size prefix but is not a required startup
callback: if it is NULL, empty-locale selection fails rather than guessing or
using the host. Explicit C/POSIX and queries still work. No ordinary `getenv`
wrapper or host `setlocale`, environment, encoding, collation, or formatting
implementation is added.

## Red and validation

Baseline: `make LDLIBS=-lucontext clean test` passed in a disposable Linux copy
on the existing CI-agent image.

Actual red preceded the implementation body: after declaring the minimal private
interface, an ordinary source probe called `setlocale(LC_ALL, "")`. Running
`make build/libcannedbsd.a` then
`cc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude -Ilibc/include tests/libc_locale_probe.c build/libcannedbsd.a -o /tmp/locale-probe`
failed at link with `undefined reference to cb_libc_setlocale`, exit 1. The
archive and other prerequisites built successfully. Later environment and
interleaving tests are additional regression coverage, not separate red-first
claims.

Focused green: `make LDLIBS=-lucontext build/test_core`, then
`build/test_core --locale` and `build/test_core --mac-acceptance`: passed.
Tests assert registration/boot/child exit statuses, explicit/query results,
unchanged errno, unsupported names/categories, empty variables, LC_ALL priority,
every category override, old API prefix and missing getenv, and two tasks with
different environments interleaved around retained query results.

The full gate `make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed locally,
including all existing tests, sanitizer/static-analysis and private-symbol
boundary checks.
Exact pushed-commit `ci`, `mac68k`, and `mac-automation` results are required at
handoff; local builds are not substitutes. Guest acceptance is pending the
coordinator's serialized slot. Three added ordinary-source Mac cases exercise
basic C-only behavior, LC_ALL precedence, and mixed-category rejection; the
shared transcript now has 21 records including contexts. The dirname utility
itself still requires the separate libc prerequisite and later unchanged import.
