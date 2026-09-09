# Solaris 9 SPARC portability gate (integration pending)

Status: **native evidence verified; final main integration pending**.
The native build and runtime passed at
`698541f1ce96df7c600b463875548af6281f446f`, containing main `eaff869`.
The coordinator verified all 326 staged source files against that commit and
retained the complete raw transcript, hashes and compiler environment. Its
note-only child `4060ab0` passed all three Woodpecker workflows (#395) and
fresh Mac acceptance: 67 records, normal shutdown, 21.68 seconds. See
`notes/iterations/SOLARIS-01.md` for exact evidence; earlier preparation
sections there are historical. Final integration CI and Mac acceptance remain
separate gates before main advances.

## What this adds

A small set of `#ifdef CANNEDBSD_SOLARIS9`-guarded blocks in
`src/host_linux.c`, plus a new `compat/solaris9/include/stdint.h`
adapter, reached only when built with `-DCANNEDBSD_SOLARIS9
-Icompat/solaris9/include`. Neither is reachable from the default
build: `CANNEDBSD_SOLARIS9` is undefined, and the compat header is
outside the default include path. The Linux adapter also explicitly clears
`uc_stack.ss_flags` when creating a context. Mac68k uses its own host adapter.
The exact Woodpecker checks above passed; this does not substitute for Mac
guest execution of the shared-source changes.

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
always holds; on ILP32 the actual, only-reachable effect is a spurious
early test failure (`return 192`, since an empty file's real `read()`
cleanly returns `0`, not the assumed `-1`/`EINVAL`) -- not a crash or a
buffer overrun; an earlier draft of this document overclaimed the
latter before actually tracing the control flow. Split into an
`#if`/`#else` pair regardless, as good practice: the ILP32 branch
exercises a real, safe, still-meaningful boundary (an offset at
`SIZE_MAX` via the fixed-width 64-bit `cb_off_t`) instead of ever
depending on the untested write-side path. See the iteration note for
the full, corrected reasoning.

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

## Build entry point

Use the verified invocation below. GNU make and the compatible pathname tools
must resolve through the recorded PATH; do not assume the stock guest defaults.
The script defaults CC to gcc and MAKE to make.

Uses the Makefile's `CC`/`CPPFLAGS`/`CFLAGS` override points and the added
`HEAD_STACKFLAGS` override (empty for GCC 3.4.6, which lacks
`-fstack-usage`). Linux's default and the separate Retro68 build retain
their stack-usage reports; this Solaris build does not emit one. The script
deliberately does **not** invoke
`gmake test`: that target pulls in `check-architecture` (uses `rg`,
not assumed present in the guest) and `tests/test_one_process.sh`
(reads Linux's `/proc`), neither applicable here. It instead builds
and runs only the shared, portable runtime surface directly -- the
core test binary, the launcher, and the same acceptance-output
assertions the historical reference used as its own `test-runtime`
gate -- see the script's own comments. Requires GCC 3.4.6, GNU make
3.81, and `librt` in the guest; see the historical reference's own
qualification notes for package provenance (Sunfreeware Solaris 9
SPARC archives). The final native transcript and compiler/make version capture are verified
and identified by hash in the coordinator acceptance record.

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
  fixed-width types, plus a missing `SIZE_MAX`. A reported native build
  exposed recursion through this project's private `inttypes.h`; the
  adapter now includes `/usr/include/inttypes.h` directly to select the
  guest's system header. That path is specific to this native Solaris
  build and is not added to default Linux or Mac68k include paths.
- `-std=gnu99 -D__EXTENSIONS__`, not strict `-std=c99`: the historical
  reference recorded that GCC 3.4.6 needs GNU C99 mode to expose the
  original Solaris headers' own 64-bit integer types.
- ILP32 `size_t`-vs-`INT64_MAX` tautological comparisons (see "What
  this adds" above for the exact risk this posed to an existing test).

## Not claimed

Final main integration, automated Solaris CI and a Solaris compiler stack-usage
report remain pending. Native source identity and execution are verified.

`CB_MAX_PROGRAMS`'s 64-slot capacity, WRITE retry/error semantics, and the
public ABI are unchanged. Test repairs retain the LP64 bounds assertions,
select reachable ILP32 assertions at preprocessing time, replace aggregate
zero initializers with `memset`, and preserve the invalid-stream test through
an intermediate `void *` cast. The three pre-existing, non-Solaris-specific
issues named above
(`ramfs_open`, `run_pipeline`, `src/vfs.c` path resolution) are real and
still open on current `main`; they are out of scope for this task and
not fixed here.

## Verified native invocation

The coordinator has verified the retained native evidence for full commit
`698541f1ce96df7c600b463875548af6281f446f`; see the final coordinator section
in `notes/iterations/SOLARIS-01.md` for archive and full-log hashes. The guest
used `PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin`, resolving GCC 3.4.6
and GNU Make 3.81 from `/usr/local/bin`. From the extracted source directory:

```sh
PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin
export PATH
CC=gcc MAKE=make /bin/ksh tools/solaris9-build.sh
```

The captured run passed the complete shared core and launcher gates and all
three output assertions. This verifies that candidate's native execution;
main integration and future automated SOLARIS-02 CI remain separate gates.
