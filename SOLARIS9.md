# Solaris 9 SPARC portability gate (in progress)

Status: **source audit and minimal build preparation only**. No native
Solaris 9 execution has been performed from this worktree. Do not read
this document as claiming guest acceptance; see
`notes/iterations/SOLARIS-01.md` for exact status and evidence.

## What this adds

A small set of `#ifdef CANNEDBSD_SOLARIS9`-guarded blocks in
`src/host_linux.c`, plus a new `compat/solaris9/include/stdint.h`
adapter, reached only when built with `-DCANNEDBSD_SOLARIS9
-Icompat/solaris9/include`. Neither is reachable from the default
build: `CANNEDBSD_SOLARIS9` is undefined, and the compat header is
outside the default include path, so ordinary Linux and Mac68k builds
run the identical `host_linux.c` code path as before -- verified by a
full `make test`/`make ci` run on the pinned Linux CI toolchain with no
behavior change (see the iteration note).

Also present: `#if SIZE_MAX > INT64_MAX` guards added around two
tautological (on a 32-bit `size_t` host) bounds checks in
`src/core.c` (`api_read`/`api_write`) and `libc/cb_libc.c`
(`cb_libc_fread`). **This one is not inert on every platform**: on
Linux (`SIZE_MAX > INT64_MAX` true there), the guarded code compiles
identically to before. On Mac68k (also a 32-bit `size_t` host), the
guard is false, so the check compiles *out* -- this silences the
`-Wtype-limits` "comparison is always false" warning already directly
observed, unguarded, in a real Mac68k build of this same code, and
does not change runtime behavior (the check was unreachable there
either way, since a 32-bit `size_t` can never actually exceed
`INT64_MAX`), but it does change Mac68k's compiled object code. A
corresponding pre-existing test (`tests/test_core.c`'s
`overflowprobe_main`) assumed a host where `SIZE_MAX > INT64_MAX`
always holds; it is now split into an `#if`/`#else` pair so its ILP32
branch exercises a real, safe, still-meaningful boundary (an offset at
`SIZE_MAX` via the fixed-width 64-bit `cb_off_t`, not an unsafe
huge-count call against a 1-byte buffer) instead of silently assuming
LP64. See the iteration note for the full reasoning and the exact
danger this avoided.

This intentionally does **not** rename `src/host_linux.c` or
`cb_linux_host_ops()`, unlike the historical reference branch
(`work/SOLARIS-01-reference`, `a28f9ed`), which renamed the file to
`host_posix.c` and touched `src/main.c`/`src/internal.h`/the Makefile's
forbidden-symbol architecture check accordingly. That rename is
avoidable churn for what is otherwise a same-file conditional adapter,
and the coordinator's own instruction for this task was to reconcile
only necessary changes against current `main`, not reproduce the
reference wholesale.

Also intentionally **not** carried forward from that reference: its
`src/vfs.c`/`src/core.c` path-resolution rewrite (`.`/`..` traversal
handling, directory-type checks), and small fixes at `src/ramfs.c`
(truncating before allocation can fail) and `src/shell.c`
(initializing all redirection descriptors before opening any, so a
partial failure doesn't leave later slots as uninitialized stack
garbage during cleanup). **These are excluded from this branch as
out of scope for a portability task, not because they have already
landed elsewhere.** A direct check of current `src/ramfs.c`'s
`ramfs_open` and `src/shell.c`'s `run_pipeline` confirms neither fix is
present on current `main` today -- they remain real, open, pre-existing
issues, independent of Solaris. An earlier draft of this document
incorrectly claimed `VFS-02`/`VFS-03` superseded them; that claim was
unverified and wrong, and has been corrected here after actually
reading the current source.

## Build (not yet executed against a real guest)

```sh
/bin/ksh tools/solaris9-build.sh
```

Uses the Makefile's existing `CC`/`CPPFLAGS`/`CFLAGS` override points
directly (no Makefile edits), and deliberately does **not** invoke
`gmake test`: that target pulls in `check-architecture` (uses `rg`,
not assumed present in the guest) and `tests/test_one_process.sh`
(reads Linux's `/proc`), neither applicable here. It instead builds
and runs only the shared, portable runtime surface directly -- the
core test binary, the launcher, and the same acceptance-output
assertions the historical reference used as its own `test-runtime`
gate -- see the script's own comments. Requires GCC 3.4.6, GNU make
3.81, and `librt` in the guest; see the historical reference's own
qualification notes for package provenance (Sunfreeware Solaris 9
SPARC archives) -- reproduced informationally in the iteration note,
not re-verified here.

`-Ilibc/include` is deliberately **not** added to this script's global
`CPPFLAGS`: that directory holds private NetBSD-import veneer headers
(its own `string.h`/`stdio.h`/`stdint.h`/etc.), already added
per-command-object by the Makefile's own existing rules for the
specific sources built against that veneer. Adding it globally would
redirect ordinary sources' standard-header includes through that
private veneer instead of the guest's real system headers, for every
compilation unit -- not what the veneer is for.

## Adaptations audited (see notes/iterations/SOLARIS-01.md for detail)

- `makecontext`'s original (pre-Solaris-10) stack-pointer convention:
  high address, not base.
- `gethrtime()` as the monotonic clock source (`clock_gettime`'s
  `CLOCK_MONOTONIC` support is not assumed present/correct at this
  vintage).
- A private `stdint.h` over Solaris 9's `inttypes.h`-only exposure of
  fixed-width types, plus a missing `SIZE_MAX`. Its actual interaction
  with the guest's real system header layout (does `-Icompat/solaris9/
  include` risk shadowing anything else the guest's own headers pull
  in transitively?) has not been independently verified against a real
  guest yet -- flagged as an open item, not assumed safe.
- `-std=gnu99 -D__EXTENSIONS__`, not strict `-std=c99`: the historical
  reference recorded that GCC 3.4.6 needs GNU C99 mode to expose the
  original Solaris headers' own 64-bit integer types.
- ILP32 `size_t`-vs-`INT64_MAX` tautological comparisons (see "What
  this adds" above for the exact risk this posed to an existing test).

## Not claimed

No native Solaris 9 build, boot, or test run. No guest console/media
access from this worktree. `CB_MAX_PROGRAMS`'s 64-slot capacity, the
WRITE runtime (`cb_libc_fwrite`/`cb_libc_fread` retry/error semantics),
and the public ABI are unchanged. All existing tests pass unchanged on
Linux (verified); the one `tests/test_core.c` change described above
adds an ILP32-specific branch without altering the LP64 (Linux) branch
at all. The three pre-existing, non-Solaris-specific issues named above
(`ramfs_open`, `run_pipeline`, `src/vfs.c` path resolution) are real and
still open on current `main`; they are out of scope for this task and
not fixed here.
