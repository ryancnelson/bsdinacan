# FILEUTIL-01: measure the file-manipulation utility set

- Status: audit complete, awaiting coordinator review
- Base SHA: `558bf31` (freshly fetched `origin/main`)
- Branch: `work/FILEUTIL-01`
- Hypothesis (as stated in `BACKLOG.md`): the pinned NetBSD `cat`, `cp`, `mv`,
  `rm`, `rmdir` and `ls` sources divide into a set buildable on the current
  descriptor/VFS surface and a set gated behind `fts(3)`, terminal width, or
  password/group lookup, and the division can be established by actual
  compile diagnostics rather than reading.
- Result: **falsified as stated.** No utility in this set is buildable now.
  The real gating subsystems are `fts(3)` (as expected) plus `sys/extattr.h`
  (extended attributes — not named in the original hypothesis), and the
  named ABI gap is narrower than expected (`rmdir` only reaches one missing
  syscall plus one missing string primitive; `mv`'s first blocker is extattr,
  not `rename`). Full evidence and reasoning below.

## Scope discipline

Documentation-only. No file under `upstream/`, `include/`, `libc/`, or `src/`
was added or modified on this branch. All pinned NetBSD source used for
compile testing was fetched to a disposable location outside the repository
(`/private/tmp/.../fileutil01-src`, not committed, not part of this worktree)
and discarded after the diagnostic runs below. `git status` on this branch
shows only this file as new/changed.

## Exact source, revision, and license

All files fetched from `https://github.com/NetBSD/src` at the project's
existing pinned revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` (verified
against this revision's own `usr.bin/head/head.c`, whose hash reproduces the
`UPSTREAM.md`-recorded `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`
exactly, confirming the fetch method and revision are correct). Note: `ls` in
NetBSD lives at `bin/ls`, not `usr.bin/ls` — the latter path returns 404 at
this revision (`usr.bin` has no `ls` entry; verified via the GitHub tree API,
263 sibling entries, none named `ls`). `bin/` also holds `cat`, `cp`, `mv`,
`rm`, and `rmdir` for this NetBSD tree.

| Utility | File | RCS revision | SHA-256 | License |
| --- | --- | --- | --- | --- |
| cat | `bin/cat/cat.c` | 1.60, 2023-12-10 | `2cc2ced0fcc6c143e1406e64cdd64ea768101fcd19b6dad531b911a611697cbd` | 3-clause Regents (1989, 1993) |
| cp | `bin/cp/cp.c` | 1.63, 2024-06-07 | `fef86b0fbc0161c436a5b6e4c8255c19e86cd9033b71f72f81a3963551d06613` | 3-clause Regents (1988, 1993, 1994) |
| cp | `bin/cp/utils.c` | 1.50, 2024-01-15 | `d20b071192c52f559082183fda12692ea99b6c19beec5680a58ed3475ae99ca2` | 3-clause Regents (1991, 1993, 1994) |
| cp | `bin/cp/extern.h` | 1.18, 2020-05-16 | `6299aea50a1f960547bb0f426b3b7bfaed614258aba488287c4efbdc074e5ff6` | 3-clause Regents (1991, 1993, 1994) |
| mv | `bin/mv/mv.c` | 1.46, 2020-06-24 | `df5de897a14e2f8210140e468b94319eaf100018490bfebe564152bf337bfd54` | 3-clause Regents (1989, 1993, 1994) |
| rm | `bin/rm/rm.c` | 1.58, 2026-04-26 | `aebdd0b46b263ca8d5c6b12cc5bf27a21b878c5ad8230c6248583b43ace53f22` | 3-clause Regents (1990, 1993, 1994, 2003) |
| rmdir | `bin/rmdir/rmdir.c` | 1.28, 2025-05-12 | `238e02e623603a271911d9e3a49c5e27f34a0bf87abd938c6a16cc0b84d7c621` | 3-clause Regents (1992, 1993, 1994) |
| ls | `bin/ls/main.c` | 1.5, 2016-09-05 | `71b5c5347082bde72a8219174f095c13b2bec90ee5a52d0786ebc1b283bdc8dd` | **2-clause NetBSD Foundation** (1999, Luke Mewburn) |
| ls | `bin/ls/ls.c` | 1.79, 2024-12-11 | `385a3c3f495913a04127fe52e082030c57e43e5d1bacf9299b2d7b5b2597787a` | 3-clause Regents (1989, 1993, 1994) |
| ls | `bin/ls/ls.h` | 1.20, 2024-12-11 | `50610b1281ff61a6171de9c73dab7ee9861e0ab0fbd66118242786abb325dc34` | 3-clause Regents (1989, 1993) |
| ls | `bin/ls/cmp.c` | 1.17, 2003-08-07 | `6a43161605d90c8b6a69103356a83cb01dbd8b7032b342335dd04cdc2003e73a` | 3-clause Regents (1989, 1993) |
| ls | `bin/ls/print.c` | 1.59, 2024-12-11 | `012336e4f206483f4aaf31a7380889aa677f66b53fc388442766dccacd1ee8a8` | 3-clause Regents (1989, 1993, 1994) |
| ls | `bin/ls/util.c` | 1.35, 2026-08-15 | `87205a7e649375576afc954f0d58597ebb4db8383c2dbffd1b0379d320f2c88a` | 3-clause Regents (1989, 1993, 1994) |
| ls | `bin/ls/extern.h` | 1.17, 2011-08-29 | `fef4bb08e410d5b9aee230df8160391383a94d1ce2d0c6d537ad3168d1af7851` | 3-clause Regents (1991, 1993) |

`ls`'s license set is mixed: `main.c` is the 2-clause NetBSD Foundation
license (distinct copyright holder, no clause-3 endorsement restriction);
every other `ls` file and every file for the other five utilities is the
3-clause Regents license. An eventual import of any of these must retain each
file's own notice individually, exactly as `dirname`/`basename` already do
for their own mixed-license pair.

## Diagnostic method

Followed the precedent in `notes/iterations/utility-roadmap-20260908.md`
(the `head`/`echo` audit) exactly, for direct comparability:

- Compiler: GCC 14.2.0, Alpine Linux 3.22, x86_64, in a disposable
  `alpine:3.22` container (`apk add build-base` only, no other packages).
- Include path: **only** this repo's `include/` and `libc/include/`
  directories (read-only bind mounts of the exact worktree checkout at
  `558bf31`) — `compat/netbsd/include/` and `src/` were deliberately excluded,
  matching the prior audit's boundary, so the result measures the public
  veneer surface only, not import-time shims or runtime-private headers.
- Command per translation unit:
  ```sh
  gcc -D_XOPEN_SOURCE=700 -Iinclude -Ilibc/include -I<utility-dir> \
    -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 \
    -Dmain=cb_audit_main -c <file>.c -o /tmp/out.o
  ```
  (`-Dmain=cb_audit_main` only applied to the file defining `main`, matching
  Makefile convention; irrelevant to these results since none reached link.)
- Each of `cp`, `ls`, and `mv`'s multiple `.c` files was compiled
  **independently** as its own translation unit — each one's first failure is
  reported separately, since a multi-file utility isn't buildable until every
  one of its translation units is.
- All fetched sources and raw diagnostics were retained under the disposable
  scratch path for this note's writing and are not part of any commit.

## Findings by utility

### cat — **not buildable now** (falsifies the prior hypothesis)

`cat.c` never hits an unconditional missing-header wall; its first ~30 lines
of diagnostics span the whole file and name eleven distinct missing
interfaces, in source order:

1. `struct flock` (line 78) — advisory record locking is entirely absent;
   cascades into undeclared `F_WRLCK`, `F_SETLKW`, and `fcntl()` further down.
2. `strtol` (line 86) — implicit declaration; not in the current veneer
   (only `strtoimax` is implemented per `LIBC.md`).
3. `setbuf` (line 110) — not implemented (stdio is deliberately unbuffered
   per `LIBC.md`, but the source still calls the no-op-shaped setter).
4. `SEEK_SET` (line 128) — not defined anywhere on the include path.
5. `fileno` (line 239) — not implemented.
6. `struct stat` / `fstat` / `S_ISREG` (lines 248, 253, 257) — the current
   `libc/include/sys/stat.h` is a two-line stub (`DEFFILEMODE` only) that
   explicitly says "no public stat or permission APIs yet." This is notable:
   the runtime ABI (`include/cannedbsd/abi.h`) already has `fstat`/`stat`
   operations and a `struct cb_stat_v1` (`abi_version`, `struct_size`,
   `inode`, `size`, `mode`, `type` — confirmed by direct inspection, no
   timestamps, `nlink`, `uid`, or `gid`, exactly as anticipated). The gap is
   entirely in the unwritten libc-level POSIX `struct stat` veneer over that
   existing ABI, not in the runtime.
7. `O_NONBLOCK` (line 249) — not defined in the current `fcntl.h`.
8. `warnx` (line 259) — not implemented (`LIBC.md` lists `err`/`errx`/`warn`
   only).
9. `BUFSIZ` (line 285) — not defined (current `stdio.h` has no buffering
   constants at all, consistent with the unbuffered design).
10. `clearerr` (line 166) — not implemented.
11. `isascii` / `toascii` / `iscntrl` (lines 209, 212, 214) — `ctype.h`
    currently implements only `isdigit`/`isspace`.

None of these eleven is `fts`, termcap, or `pwd`/`grp` — cat's blockers are
entirely ordinary small libc/stdio/fcntl surface plus wiring the *existing*
`cb_stat_v1` ABI up to a public `struct stat`. This doesn't fit either named
bucket in the backlog's classification cleanly; see the note under
"Classification" below.

### cp — **blocked on subsystem: fts AND extattr** (two independent gates)

- `cp.c`: fatal, line 71, `#include <fts.h>` — no further diagnostics are
  reachable past a fatal missing-header error, by construction.
- `utils.c`: fatal, line 47, `#include <sys/acl.h>` under `#ifndef SMALL` (a
  real, existing NetBSD build-tuning macro, not a shim). Recompiling with
  `-DSMALL` (skipping only the ACL branch, nothing else) reaches a *second*
  fatal blocker at the same line number in the file: `#include <sys/extattr.h>`
  (line 49 in the `-DSMALL` pass), unconditional. So `cp` needs `fts(3)`
  (via `cp.c`) and, independent of ACL support, extended attributes (via
  `utils.c`) regardless of which build variant is chosen.
- Recommendation for whoever eventually claims `CP-01`: build with `-DSMALL`
  from the start — it's a real upstream NetBSD variant (used for
  install/rescue media) that cleanly drops the ACL branch, and this project
  already defers permissions/ACL enforcement entirely per `MV-01`/`RM-01`'s
  own scope notes. This does not reduce `cp`'s dependency count (extattr is
  unconditional either way) but removes one whole subsystem from scope
  honestly rather than by omission.

### mv — **blocked on subsystem: extattr** (not confirmed blocked on `rename`)

`mv.c` fails immediately and unconditionally at line 53:
`#include <sys/extattr.h>`. This is a **correction to the prior
expectation**: the actual first-observed blocker is extended attributes, not
`rename()`. Because a fatal missing-header error stops GCC before parsing any
further code, **whether `mv.c` also requires `rename()` is not established by
this evidence** — that call site (if present) is textually after line 53 and
was never reached. Asserting `mv` needs `rename()` beyond this point would be
exactly the kind of unearned inference this task's own Red step warns
against; it isn't claimed here. `VFS-04`'s `rename()` operation may well
still be required once extattr is cleared, but that is a hypothesis for
`MV-01`'s own red step to test, not a finding of this audit.

### rm — **blocked on subsystem: fts** only

`rm.c` fails immediately at line 53: `#include <fts.h>` (needed for `rm -r`'s
recursive tree walk). No extattr or ACL include appears anywhere in this
file. This is `rm`'s only observed blocker.

### rmdir — **blocked on named ABI gap, plus one small libc gap**

Two distinct, independent findings, both past the `#include` block (no
fatal header errors in this file at all):

1. Line 89: `rmdir` itself is an implicit/undeclared function call — genuinely
   absent everywhere on the include path. Confirmed independently: neither
   `rmdir` nor `rename` appear anywhere in `include/cannedbsd/abi.h` (grepped
   directly), unlike `stat`, which at least has an existing ABI entry. This
   matches the prior expectation that `rmdir` is a named ABI gap, to be
   supplied by `VFS-04`.
2. Line 109: `strrchr` is also implicitly declared/undefined — the current
   `string.h` veneer has `strlen`, `strcmp`, `memcpy`, `memmove`, `memcmp`,
   `strchr`, and `strcpy` (per `LIBC.md`), but not `strrchr`. This is a small,
   independent, previously-unlisted libc gap (`rm_path()`'s trailing-slash
   trimming loop), not part of `VFS-04`'s node/VFS ABI scope and not
   supplied by it. `VFS-04` alone will not make `rmdir.c` compile.

### ls — **blocked on subsystem: fts, unanimously across all 5 translation units**

- `main.c`, `ls.c`, `cmp.c`, `util.c`: each fails immediately and
  unconditionally at its own first `#include <fts.h>` line.
- `print.c`: fails first on `#include <sys/acl.h>` (again gated
  `#ifndef SMALL`); recompiled with `-DSMALL`, its very next line is
  `#include <fts.h>` — same subsystem, just one line later.
- No file in pinned `ls` requires `sys/extattr.h` anywhere (checked all five
  `.c` files' diagnostics and source directly) — `extattr` is specific to
  `cp`/`mv`, not `ls`.

This directly triggers the contingency already written into `LS-01`'s own
backlog scope: *"If the audit shows pinned `ls` cannot be imported without
`fts(3)`, this becomes a cannedBSD-owned command using the existing dirent
contract, and unchanged pinned `ls` gets a separate later ID."* That
condition is now confirmed true by direct evidence. Recommend `LS-01`
proceed exactly as its own contingency describes — a bounded, cannedBSD-owned
single-column `ls` over the existing (`Done`) `VFS-03` dirent contract, with
no dependency on an `fts(3)` implementation at all. Unchanged pinned `ls`
becomes its own later, separately-IDed task once `fts(3)` exists.

## Classification summary

| Utility | Buildable now? | Classification | Basis |
| --- | --- | --- | --- |
| cat | No | Blocked on missing ordinary libc surface (not fts/termcap/pwd-grp, not a new VFS/ABI verb) | 11 distinct missing interfaces: locking, `strtol`, `setbuf`, stdio macros, `fileno`, full `struct stat`/`fstat`/`S_ISREG`, `O_NONBLOCK`, `warnx`, `clearerr`, ctype trio |
| cp | No | Blocked on subsystem: fts **and** extattr (two independent gates) | `cp.c` → `fts.h`; `utils.c` → `sys/acl.h` (skippable, real `-DSMALL`) → `sys/extattr.h` (not skippable) |
| mv | No | Blocked on subsystem: extattr (rename status unmeasured) | `mv.c` → `sys/extattr.h`, unconditional, before any other code is reached |
| rm | No | Blocked on subsystem: fts | `rm.c` → `fts.h`, unconditional |
| rmdir | No | Blocked on named ABI gap (`rmdir`) + one small libc gap (`strrchr`) | Both confirmed as implicit/undeclared past a clean `#include` block |
| ls | No | Blocked on subsystem: fts (all 5 files, unanimous) | `main.c`/`ls.c`/`cmp.c`/`util.c` direct; `print.c` via ACL→fts fallthrough |

No utility in this set is buildable now. The backlog's third bucket
("blocked-on-subsystem: fts, termcap, pwd/grp") needs a fourth real member,
**extattr** (`sys/extattr.h`), which gates `mv` unconditionally and `cp`
independently of its `fts` gate. `cat` doesn't fit any of the three named
buckets — it's blocked on breadth of small, ordinary libc/ABI-wiring gaps,
none of them a subsystem and none of them a new VFS verb.

## Recommended claim order

Based on actual dependency weight observed above, layered against the
backlog's existing `Depends on` edges (`MV-01` and `RM-01` already both
require `CAT-01` done, independent of this audit):

1. **`LS-01`** — no subsystem dependency at all once redirected to its own
   documented contingency (cannedBSD-owned, dirent-based). Independently
   claimable now; does not wait on `fts(3)`, `extattr`, or `CAT-01`.
2. **`VFS-04`*** (rmdir/rename) — smallest true ABI-gap surface of the six,
   but note it must also budget for `strrchr` (not in its current scope
   text) or `rmdir.c` still won't compile after `VFS-04` lands alone.
3. **`CAT-01`** — no new subsystem, but the widest small-interface surface
   (11 items), including a real design question: whether to extend
   `cb_stat_v1` (timestamps/`nlink`/`uid`/`gid` are all absent from it today)
   before or while wiring a public `struct stat`. Both `MV-01` and `RM-01`
   already wait on this per the existing backlog graph, so it's on the
   critical path regardless of this audit's ordering.
4. **A new fts(3)-subsystem task** (not yet in `BACKLOG.md`) — unlocks
   `RM-01` completely and half of `CP-01`.
5. **A new extattr-subsystem task** (not yet in `BACKLOG.md`) — unlocks
   `MV-01`'s first blocker and the other half of `CP-01`. `MV-01` may still
   need `rename()`/`VFS-04` behind it; unmeasured, flag for `MV-01`'s own red
   step.
6. **`MV-01`** and **`CP-01`** last — both stack two dependencies each
   (extattr + possibly rename for `mv`; fts + extattr for `cp`), the most
   total prerequisite work of the six.

## Documentation validation

Staged publication-hygiene and `git diff --check` review: this note
introduces no email addresses, credentials, private network addresses, or
personal host paths (the disposable fetch/diagnostic path lives outside the
repository and is not referenced by path here beyond its `/private/tmp/...`
scratch location, which is session-local and not a durable host reference).
No runtime, libc, or ABI file changed; `git status` on `work/FILEUTIL-01`
shows only this file. Guest acceptance (Mac68k, Solaris9): not required —
documentation-only change, no runtime/libc/VFS/shell/command/host-adapter
behavior changed, per `AGENTS.md`'s own exception for such changes.
