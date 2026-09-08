# LIBGEN-01: `dirname(3)` design

- Status: design only -- no runtime implementation in this branch.
- Base SHA: `0ebfd2f` (`origin/main`, freshly fetched -- confirmed directly:
  `err(3)`/`errx`/`err` are already integrated (`cb_libc_errx`/`cb_libc_err`
  present in `libc/cb_libc.c`), and the CI01 shared-`/tmp`-isolation fix
  (`Isolate printenv test stderr captures across concurrent CI runs`) is
  already `HEAD`, so a new worktree from here already includes it.
- Branch: `work/LIBGEN-01-design`.
- Pinned upstream revision inspected: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
  (`https://github.com/NetBSD/src`), the same revision every prior pinned
  import in this repo uses (`UPSTREAM.md`).

## 1. Scope and non-goals

In scope: the smallest possible libc `dirname(3)` contract -- one
function, `char *dirname(char *path)`, matching NetBSD's own
`lib/libc/gen/dirname.c` behavior byte-for-byte (import, not reimplement).

Explicitly out of scope for this backlog item, to avoid speculative
surface:

- **`usr.bin/dirname/dirname.c`, the shell command.** Inspected (see
  below) because the assignment asked for it, but not part of this
  contract -- a command wrapper is a separate, later backlog item if
  wanted, and would be a thin, obvious layer over the libc function once
  it exists (mirroring `printenv`'s command-over-libc-surface shape).
- `basename(3)`. NetBSD ships it as a sibling function in the same
  general area (`lib/libc/gen/basename.c`), but nothing in this backlog
  item asked for it and importing it now would be exactly the
  "speculative libc expansion" this project's standing instructions
  reject. A future `LIBGEN-02` can add it using the same pattern this
  document establishes.
- `realpath(3)`, `pathconf(3)`, or any other `libgen.h`/path-utility
  function.

## 2. Provenance of the two inspected upstream files

### `usr.bin/dirname/dirname.c` (inspected only, not imported)

- Upstream path: `usr.bin/dirname/dirname.c`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- SHA-256 (as fetched): `839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`
- Embedded RCS identifier: `$NetBSD: dirname.c,v 1.12 2011/09/16 15:39:25 joerg Exp $`
- License: file-specific three-clause Regents of the University of
  California license.
- Relevance: it is a 10-line wrapper -- `getopt(argc, argv, "")` (accepts
  no options at all), `setlocale(LC_ALL, "")`, one call to `dirname()`,
  `err(1, "%s", *argv)` if that call returns `NULL` (which, per the
  implementation below, it structurally cannot in this revision -- dead
  code, faithfully present upstream regardless), then
  `printf("%s\n", p)`. Confirms the command adds no behavior beyond the
  libc function plus argv/usage handling, which is why it is deferred
  rather than imported now.

### `lib/libc/gen/dirname.c` (the file this design proposes importing)

- Upstream path: `lib/libc/gen/dirname.c`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- SHA-256 (as fetched): `05ad1f66a7a5a4ceee33fe767a3410c60aa76e4ed670b5d57ab9198a0a2a892b`
- Embedded RCS identifier: `$NetBSD: dirname.c,v 1.14 2018/09/27 00:45:34 kre Exp $`
- License: file-specific two-clause NetBSD Foundation license (distinct
  from the Regents license on the command and on `strlen`/`strcmp`/etc.;
  must be retained and cited separately in `UPSTREAM.md`, not merged into
  the existing Regents-license boilerplate).
- Structure: a `static size_t xdirname_r(const char *path, char *buf,
  size_t buflen)` helper that does the actual work into a caller-supplied
  buffer, and the exported `char *dirname(char *path)` (guarded by
  `#if !HAVE_DIRNAME`) that calls it with a **function-local `static char
  result[PATH_MAX]`** and returns a pointer into that static. `xdirname_r`
  itself is `static` (no external linkage) -- it cannot be selectively
  pulled out and reused stand-alone without editing the file, which this
  project's byte-for-byte-import discipline forbids. Section 4 below is
  about why that specific shape (an internal reentrant helper, wrapped by
  a non-reentrant exported static-buffer function) is exactly the hazard
  this design has to account for.

## 3. The `dirname()` contract, and pathname edge cases

Byte-for-byte behavior of the imported `xdirname_r`/`dirname` (verified by
reading the fetched source, not by speculation):

- `path == NULL` or `path` is `""`: result is `"."`.
- No `/` anywhere in `path` (e.g. `"foo"`): result is `"."`.
- `path` is exactly `"/"`, or is entirely slashes (e.g. `"////"`): result
  is `"/"`.
- Trailing slashes are stripped first (`"/foo/bar///"` behaves as
  `"/foo/bar"`), *then* the last component is removed, so
  `"/foo/bar///"` produces `"/foo"`.
- Ordinary case (`"/foo/bar"`): result is `"/foo"`; a relative path with
  one component (`"foo/bar"`) produces `"foo"`.
- The function only ever reads `path`; despite the non-`const`
  historical POSIX signature, this specific implementation never writes
  through it -- safe to treat as read-only.
- The function **cannot fail**: every input, including `NULL`, produces
  a valid string. There is no error path, no `errno` is ever set by
  `xdirname_r`/`dirname` itself. (This is what makes the command
  wrapper's `err(1, ...)` on a `NULL` return dead code, noted above.)
- Truncation: `xdirname_r`'s `buf`/`buflen` path (used internally with
  `buf = result`, `buflen = sizeof(result)`) clamps via
  `MIN(len, buflen - 1)` and always NUL-terminates -- silent truncation
  for a path whose dirname exceeds the buffer, not a reported error. This
  matters directly for the buffer-size decision in section 4.

## 4. Task-local output lifetime -- the central design problem

`dirname()`'s exported entry point returns a pointer into a **process-wide
static buffer internal to that one function**, filled in on every call.
On a traditional single-tasked Unix process this is safe by convention
(the caller is expected to consume or copy the string before the next
`dirname()` call). cannedBSD is not single-tasked: many cooperatively
scheduled tasks share one address space, and control can pass between
them at yield points. If this file were imported and its `dirname` symbol
exposed directly (the way `strlen`/`strcmp` are, via a link-name
`-D`/adapter), the static buffer would be **one buffer shared by every
task in the process**, not one buffer per task -- exactly the same class
of hazard `PENV-01` fixed for `environ` and this project's own
`errno`/`getopt` state already avoid by being task-owned. Concretely: if
task A calls `dirname()`, holds the returned pointer without immediately
consuming it, and then does anything that can yield (blocking I/O,
`cb_yield`) before reading through that pointer, task B's own intervening
`dirname()` call silently overwrites A's result out from under it. This
is worse than merely "the same task's next call invalidates the
previous one" (which matches upstream's own documented convention and is
fine to inherit) -- it is cross-task corruption of a value the caller
never reused or raced on itself.

**Decision**: import `lib/libc/gen/dirname.c` byte-for-byte (unmodified,
matching the `strlen`/`strcmp`/`memcpy` precedent), but do **not** expose
its `dirname` symbol to ordinary programs directly. Compile it with a
private link name via a plain `-D` object-like macro --
`-Ddirname=cb_libc_dirname_upstream` -- exactly the same mechanism
already used for `strlen` (`-Dstrlen=cb_libc_strlen`); confirmed safe by
reading the file for any `#undef dirname` or macro games that would
defeat a `-D` rename (there are none -- the only other occurrence of the
literal text `dirname` is inside `__weak_alias(dirname,_dirname)`, itself
inside `#ifdef __weak_alias`, and this project's minimal `namespace.h`
shim never defines `__weak_alias`, so that whole line is preprocessed out
before macro substitution would matter; `strcmp`/`memcpy`'s heavier
`__strong_alias`/asm-label adapter is not needed here). `cb_libc_dirname_upstream`
keeps its NetBSD-internal `static char result[PATH_MAX]` -- still one
shared buffer process-wide -- but nothing outside `libc/cb_libc.c` ever
sees a pointer into it directly.

`libc/cb_libc.c` then provides the real, task-safe veneer:

```
char *cb_libc_dirname(char *path)
{
    const char *shared;
    char *owned;
    if (!dirname_api_available())        /* struct_size + non-NULL, ENOSYS
                                             pattern established in VFS-03 */
        return NULL;                     /* errno = ENOSYS */
    shared = cb_libc_dirname_upstream(path);
    owned = bound_api->dirname_buffer_location();
    /* Copy out of the shared upstream static into this task's own buffer
       before returning, so a task that does not immediately consume the
       result cannot have it overwritten by another task's unrelated call.
       This does not need locking: only one task ever executes at a time
       (cooperative scheduling), so the copy itself cannot race -- the
       hazard is purely about what happens *after* this function returns,
       which per-task ownership of the destination buffer solves. */
    cb_libc_strlcpy_or_equivalent(owned, shared, CB_PATH_MAX);
    return owned;
}
```

This mirrors the existing `errno`/`environ`/`getopt` pattern exactly: a
new, append-only `cb_api_v1` accessor,

```c
char *(*dirname_buffer_location)(void);
```

placed after `closedir` (the current last field), returning a pointer to
one new field on `struct cb_task`:

```c
char dirname_buffer[CB_PATH_MAX];
```

No separate allocation, retain/release, or cleanup path is needed for
this field -- it is inline storage on the task struct itself (exactly
like `struct cb_getopt_state_v1 getopt_state`), so it lives and dies with
the task automatically; `task_destroy` needs no new code, unlike
`directories[CB_MAX_DIRS]` (VFS-03), which held an external retained
resource requiring explicit release.

**Old-table compatibility**: `dirname_buffer_location` is an optional
ABI tail exactly like `opendir`/`readdir`/`closedir` (VFS-03) --
`cb_libc_dirname` must check a `dirname_api_available()`-style guard
(`struct_size` covering the field, and the pointer itself non-`NULL`)
before calling it, returning `ENOSYS` on an older or shrunk table, per
this project's now-established append-only-tail discipline. This needs
the same three coverage shapes VFS-03's fourth review pass established:
a shrunk-`struct_size` old-table case, a full-size-but-`NULL`-field case,
and ordinary-source coverage of both (see section 8).

**Buffer size**: `CB_PATH_MAX` (1024, `include/cannedbsd/abi.h`) is used
for the task-owned buffer rather than importing or inventing a smaller
`PATH_MAX`, for the same reason VFS-03's design review rejected an
invented smaller `d_name` bound -- one existing, already-pinned constant,
not a second one to keep in sync. Upstream's own `xdirname_r` truncates
safely into whatever buffer it is given (section 3), so copying into a
`CB_PATH_MAX`-sized destination is never narrower than upstream's own
`PATH_MAX`-sized static in any environment this project targets.

## 5. New shim headers required (none exist yet)

`lib/libc/gen/dirname.c` includes `"namespace.h"`, `<sys/param.h>`,
`<libgen.h>`, `<limits.h>`, and `<string.h>`. Checked against the current
tree: `libc/include/` and `compat/netbsd/include/` have neither
`sys/param.h`, `libgen.h`, nor `limits.h` yet (only
`compat/netbsd/include/namespace.h` and `assert.h` exist, both already
established as minimal import-only shims for the string routines).
Implementation will need three new minimal shims, following that exact
precedent -- import-only, no unimplemented surface advertised:

- `compat/netbsd/include/sys/param.h`: only needs to supply the `MIN`
  macro `xdirname_r` uses; must not pull in anything else NetBSD's real
  `sys/param.h` exposes (page sizes, `howmany`, etc.) that this project
  does not implement.
- `compat/netbsd/include/limits.h` (or `libc/include/limits.h`, TBD at
  implementation time depending on whether NetBSD's `dirname.c` needs
  `PATH_MAX` to already agree with `CB_PATH_MAX` at compile time -- if
  so, this header should `#define PATH_MAX CB_PATH_MAX` by including
  `cannedbsd/abi.h`, keeping one source of truth rather than a second
  pinned constant): only `PATH_MAX`.
- `compat/netbsd/include/libgen.h`: only the declaration `dirname.c`
  implicitly relies on being internally consistent with its own
  definition (an empty or near-empty import-only header, matching the
  existing `assert.h` shim's precedent of "prevents an incidental
  dependency on the build host's copy without advertising an
  unimplemented cannedBSD API").

The genuinely ordinary-facing header, `libc/include/libgen.h`, is a
*separate* file from the compat shim above (same split already
established for `dirent.h` vs. `cannedbsd/libc.h`): it declares
`char *dirname(char *path);` mapped via `#define dirname cb_libc_dirname`,
matching every other ordinary-facing header in this codebase (`err.h`,
`dirent.h`).

## 6. C-locale dependency: none, for the function actually in scope

The *command* (`usr.bin/dirname/dirname.c`) calls `setlocale(LC_ALL, "")`
-- irrelevant here since the command is out of scope (section 1). The
*libc function* (`lib/libc/gen/dirname.c`) has **no locale dependency at
all**: `xdirname_r` operates purely on the byte values `'/'` and `'\0'`,
with no `ctype`/multibyte awareness anywhere in it. cannedBSD's total
absence of a locale subsystem is therefore a non-issue for this backlog
item's actual contract -- worth stating explicitly rather than leaving it
an open question, since the assignment specifically asked about it.

## 7. Allocation, exec, and exit lifetime

Because the task-owned buffer is inline storage on `struct cb_task`
(section 4), not a separately allocated or externally retained resource:

- **Allocation**: none beyond the task struct's own allocation at
  creation; no failure mode to handle (unlike `directories[]`'s
  `cb_allocate` calls in VFS-03, this has none).
- **`exec`**: `task_finish_exec` needs no new code. The buffer is
  reinitialized implicitly the next time `cb_libc_dirname` is called
  (its old contents from before the exec are simply stale, unread,
  harmless bytes -- there is no concept of a "dirname result surviving
  exec" for any real program, since `dirname()`'s contract never promised
  a result would remain valid past any subsequent call, let alone an
  exec). No close-on-exec-style question applies, unlike descriptors or
  directory handles.
- **`exit`/`task_destroy`**: nothing to release -- the buffer's memory is
  reclaimed automatically as part of freeing `struct cb_task` itself,
  exactly like `getopt_state`.

This is a meaningfully smaller lifetime story than VFS-03's directory
handles, and worth stating plainly rather than padding this document with
unneeded ceremony: the entire cleanup story is "nothing to do," because
nothing external is ever retained.

## 8. Falsifiable test plan

All of the following are concrete, checkable assertions -- not aspirational
claims -- for the implementation phase:

1. **Ordinary-source red-first probe**: a `tests/libc_dirname_probe.c`
   calling `dirname()` on a few fixed inputs, compiled against a
   `cb_libc.c` with `cb_libc_dirname` not yet defined, must fail to
   *link* (matching this project's established "declared but not yet
   defined" red pattern from every prior libc addition), not merely fail
   at runtime.
2. **Pathname edge cases** (section 3), each as a direct assertion once
   green: `NULL`, `""`, `"foo"` (no slash), `"/"`, `"////"`,
   `"/foo/bar"`, `"/foo/bar///"` (trailing slashes), `"foo/bar"`
   (relative), and a path whose directory component is exactly
   `CB_PATH_MAX - 1` bytes (boundary, proves the copy-out does not
   off-by-one truncate a case upstream itself would not have truncated).
3. **Task-local isolation, the actual point of section 4**: two
   concurrently scheduled tasks (the existing past-pipe-capacity
   blocking pattern already used for `direntisolationprobe` and
   `environpeer`/`environprobe`) each call `dirname()` on a *different*
   path, forced to interleave around a yield point, and each must
   observe **its own** result afterward, not the other task's. This is
   the test that would fail immediately if `cb_libc_dirname` were
   implemented naively as "just return `cb_libc_dirname_upstream(path)`
   directly" instead of copying into task-owned storage -- the single
   most important test in this plan.
4. **Repeated same-task calls**: a task calling `dirname()` twice in a
   row with different inputs must see the *second* call's result, not
   the first (matches upstream's own single-buffer-per-call convention,
   just confirming the veneer does not accidentally cache or freeze the
   first result).
5. **Old-table / null-tail coverage** (mirroring VFS-03's fourth review
   pass exactly, per section 4's old-table paragraph): a shrunk-
   `struct_size` copy stopping before `dirname_buffer_location`, a
   full-size copy with that one field explicitly `NULL`, and an
   ordinary-source probe expecting `ENOSYS` for both, run through a
   raw-ABI orchestrator the same way `direntoldtableprobe`/
   `direntnulltableprobe` do.
6. **Boundary/architecture checks**: `tests/test_libc_source.sh`-style
   scan of the new ordinary probe rejecting `cannedbsd`/`internal.h`/
   `cb_*` names, requiring it to call `dirname()`, and requiring its
   compiled object to import the private `cb_libc_dirname` veneer symbol
   rather than any host-facing name -- exact mechanical repeat of the
   existing `dirent_source`/`dirent_object` pattern.
7. **Provenance/hash pin**: `tests/test_netbsd_libc_source.sh`-style
   pinned SHA-256 and embedded RCS-identifier check on the imported
   `lib/libc/gen/dirname.c`, matching the existing `strlen`/`strcmp`/
   `memcpy`/`memmove`/`memcmp`/`strchr` entries.

## 9. Explicitly not claimed

- `usr.bin/dirname` (the command), `basename(3)`, `realpath(3)`, and any
  other `libgen.h`/path function: out of scope (section 1), future
  backlog items.
- No claim that `dirname()`'s result can safely outlive a subsequent call
  *by the same task* -- that non-reentrant-per-task convention is
  inherited from upstream deliberately, matching real BSD `dirname(3)`'s
  own documented contract. Only cross-task isolation is fixed here.
- No locking or atomicity claims beyond cooperative scheduling's existing
  single-task-runs-at-a-time guarantee (section 4) -- there is no
  preemption in this runtime to protect against mid-copy.
