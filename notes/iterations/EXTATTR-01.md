# EXTATTR-01 — narrow unsupported attribute-copy boundary

Status: design decision and diagnostic experiment complete; ready for review.
Base: `7b8fa78ac5bd999c42aa6422dbdc206b0fee3032` (`origin/main`).
Branch: `work/EXTATTR-01`. Documentation only; no production header or stub added.

## Decision

Do not build an extended-attribute subsystem for the file-manipulation milestone.
Authorize a subsequent bounded libc slice exposing just:

```c
int fcpxattr(int from_fd, int to_fd);
```

Its private cannedBSD implementation must return `-1` and set task-local
`errno = ENOSYS`, without copying anything or mutating either descriptor.
ENOSYS is already present in the libc veneer; this decision requires no new
errno or runtime ABI field. The header must map the ordinary source name to
its private cannedBSD symbol, following existing veneer conventions.

This is an explicit unsupported operation, not a successful no-op. Do not
return zero based on an assumption that neither filesystem has attributes.
Do not add namespace constants, list/get/set APIs, or an attribute store.
Descriptor validation is not part of this unsupported-operation slice; it
returns ENOSYS regardless of its integer arguments.

The follow-on implementation needs one focused ordinary-source check of the
return value and task-local errno, plus existing project gates. No broad
attribute edge-case matrix is needed for a feature we do not implement.

## Pinned source evidence

Revision: NetBSD/src `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
Sources were fetched outside the repository; the utility hashes independently
match FILEUTIL-01. No imported source was edited or added here.

| File | SHA256 |
| --- | --- |
| bin/mv/mv.c | df5de897a14e2f8210140e468b94319eaf100018490bfebe564152bf337bfd54 |
| bin/cp/utils.c | d20b071192c52f559082183fda12692ea99b6c19beec5680a58ed3475ae99ca2 |
| bin/cp/extern.h | 6299aea50a1f960547bb0f426b3b7bfaed614258aba488287c4efbdc074e5ff6 |
| bin/mv/pathnames.h | 82819eb682b4e2e8ec84604d8b9b11532f987427d697989ff21c6aa56fc30d6a |
| sys/sys/extattr.h | 63482a642dfcaa79cf4f3c3cd8defacaff4fb8c09e700d6c99fd79469684e875 |

The exact header declares `int fcpxattr(int _from_fd, int _to_fd)` at line 119.
The two utility translation units reference no other extended-attribute API.

- `mv.c:231-239`: successful rename returns immediately. Non-EXDEV failure
  reports a rename error. Attribute copying is not on the successful
  same-filesystem rename path.
- `mv.c:323-324`: regular-file cross-filesystem fastcopy calls fcpxattr,
  warns on -1, then continues. The warning alone does not stop the move.
- `utils.c:258-259`: cp calls fcpxattr only when pflag is set; failure warns
  but does not set rval. Subsequent metadata handling can fail separately.
- `utils.c:261-264`: SMALL excludes ACL preservation, not the fcpxattr call.

Consequence: unsupported attribute copying is visible, but unchanged upstream
code does NOT guarantee a nonzero exit for that warning alone. Do not advertise
full metadata preservation or rely on that warning to protect a cross-mount
move. Initial acceptance should target same-filesystem mv and ordinary cp;
metadata-preserving/cross-mount semantics require explicit later scope.

## Measured compile iteration

Hypothesis: a declaration-only scratch header removes the immediate extattr
include wall without requiring the full subsystem. It will not make either
utility buildable by itself.

Alpine 3.22 build-base compiler, using the same public-veneer-only method as
FILEUTIL-01 (no runtime-private or import compatibility includes):

```sh
gcc -D_XOPEN_SOURCE=700 -DSMALL -Iinclude -Ilibc/include -I"$AUDIT" \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 \
  -Dmain=cb_audit_main -c "$AUDIT/mv.c" -o "$AUDIT/mv.o"
```

Repeat for utils.c. Baseline exits 1 for both:

```text
mv.c:53:10: fatal error: sys/extattr.h: No such file or directory
utils.c:49:10: fatal error: sys/extattr.h: No such file or directory
```

Then prepend `-I"$AUDIT/shim"`, containing ONLY a guarded declaration of
fcpxattr. This is a diagnostic shim, not a linked implementation or a claimed
feature. Both still exit 1:

```text
utils.c:54:10: fatal error: fts.h: No such file or directory
```

The first mv pass stopped on a missing local `pathnames.h`; that was a fetch
setup omission, not a cannedBSD gap. After fetching the exact pinned local
header, compilation reached actual missing veneer surface:

```text
mv.c:74:49: error: 'struct stat' declared inside parameter list ...
mv.c:78:18: error: expected ';', ',' or ')' before '__unused'
mv.c:123:22: error: 'SIGINFO' undeclared ...
mv.c:129:21: error: implicit declaration of function 'stat' ...
mv.c:136:19: error: implicit declaration of function 'strlcpy' ...
mv.c:148:26: error: implicit declaration of function 'strrchr' ...
mv.c:154:25: error: implicit declaration of function 'warnx' ...
mv.c:185:23: error: implicit declaration of function 'access' ...
mv.c:231:14: error: implicit declaration of function 'rename' ...
mv.c:237:22: error: 'EXDEV' undeclared ...
```

These are selected actual diagnostics, not an exhaustive prerequisite list.
The audit uses main's public headers, not the unmerged VFS-04 branch, and
host fallback headers remain enabled as in FILEUTIL-01. It is a dependency
probe, not proof of freestanding compilation or correct target linkage.

## Effect on ordering

1. EXTATTR-01 is a small design boundary, not a large subsystem implementation.
2. CAT-01's stat/libc work remains valuable shared groundwork for mv/cp.
3. FTS-01 still gates cp and recursive rm. RM-01 has no extattr dependency.
4. MV-01 must be rescoped from these real diagnostics before implementation;
   permissions and SIGINFO are not magically supplied by the existing narrow
   SIGINT work. Do not silently stub them or expand another worker's scope.
5. If unchanged mv remains too expensive for the next visible milestone,
   propose a separately authorized cannedBSD-owned same-filesystem command,
   rather than altering pinned upstream code or claiming full mv semantics.

## Verification boundary

No runtime behavior changed. Compile failures above are expected feasibility
evidence, not passing application tests. Publication and diff checks apply;
Mac and Solaris execution are not required for this documentation-only change.
No shared VM or guest console was accessed. No claim of upstream mv/cp build
success, runtime success, or newly implemented attribute support is made.
