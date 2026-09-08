# VFS-03 design: directory iteration and libc `dirent`

Status: design review only, per `VFS-03`'s "Blocked on VFS-01" backlog status
(VFS-01's mount routing is integrated in this base). No runtime code in this
branch.

## 1. What already exists (read before proposing anything new)

- `struct cb_vfs_node_ops` (`src/internal.h:96`) is the generic, versioned,
  per-mount-supplied table for node behavior. It already contains exactly
  the precedent this design needs: `truncate` (from the now-integrated
  `FS-01`) is an **optional, `struct_size`-guarded extension** — the field
  itself carries the comment "Optional extension; callers must check
  struct_size before reading," and the dispatch site
  (`src/vfs.c:339`-`343`) checks
  `node->ops->struct_size < offsetof(struct cb_vfs_node_ops, truncate) +
  sizeof(node->ops->truncate) || node->ops->truncate == NULL` before ever
  calling through it, returning `ENOSYS` otherwise. This is the exact,
  already-shipped mechanism `VFS-03`'s "old-table compatibility" requirement
  asks for — a new directory-iteration node op should be appended the same
  way, guarded the same way, at the same dispatch layer.
- `struct cb_ramfs_node` (`src/ramfs.c:9`) stores a directory's children as
  a **singly-linked list**, newest-first (`node_create`, `src/ramfs.c:102`:
  `node->next_sibling = parent->children; parent->children = node;`). This
  ordering (not creation order, not alphabetical) is an implementation
  detail RAMFS already exposes nowhere generically — `VFS-03` must not leak
  it as a promised order; see §3.
- `ramfs_unlink` (`src/ramfs.c:207`) **cannot remove a directory at all** in
  this codebase today — it returns `EISDIR` for an empty directory and
  `ENOTEMPTY` for a non-empty one, unconditionally. There is no `rmdir`.
  This matters a great deal for this design: **the directory node being
  iterated can never structurally disappear out from under an iterator in
  v0.1** — only the *regular files inside it* can be unlinked mid-iteration.
  This narrows "directory lifetime across unlink" to one concrete,
  bounded question (§4), not an open-ended one.
- `ramfs_unlink` splices the removed node out of its parent's list and then
  **clears its own `next_sibling` to `NULL`** (`src/ramfs.c:220`-`222`),
  even though the node itself may still be kept alive by a positive
  `references` count (the same reference-counting scheme that already
  keeps an open regular file's storage alive past unlink, per `SPEC.md`
  §7). This is the one concrete hazard this design must not walk into
  blindly — see §3 for why the recommended cursor shape sidesteps it
  instead of asking for a `ramfs_unlink` behavior change.
- Everything a directory entry needs to report is already obtainable
  through node ops that exist today: `node->ops->name(node)` for `d_name`,
  and `node->ops->stat(node, &st)` for `d_type`/`d_ino` (`st.type`,
  `st.inode`). No new stat-shaped operation is needed.
- `enum cb_node_type` (`include/cannedbsd/abi.h`) has `CB_NODE_REGULAR`,
  `CB_NODE_DIRECTORY`, `CB_NODE_TERMINAL`, `CB_NODE_PIPE` — but
  `ramfs_create` (`src/ramfs.c:185`-`205`) only ever accepts `REGULAR` or
  `DIRECTORY` for a *named* path entry. Terminals and pipes are never
  directory members. `d_type` therefore only ever needs to distinguish two
  live values, plus an "unknown" sentinel for whatever a future non-RAMFS
  mount (`/Host`, rump, persistent) might report.
- Mounting is add-only in this base (`cb_vfs_mount_path`, `src/vfs.c`) —
  there is no unmount. A mount point is just an ordinary directory node in
  its parent mount whose path resolution happens to be intercepted
  *before* reaching node ops (`resolve_normalized`); iterating the parent
  directory that contains a mount point sees it as one ordinary entry (its
  own name/type), the same way real `readdir("/")` shows `mnt` as one
  entry without descending into what is mounted there. Directory iteration
  is therefore **intra-mount by construction** — no special multi-mount
  handling is needed, and none is proposed.
- `libc/include/sys/types.h` (new in `FS-01`, currently just
  `stddef.h`/`stdint.h`) is the established place for small ordinary-facing
  type prerequisites — `ino_t` belongs there too (§6), not a new header.
- `struct cb_libc_file` (`libc/cb_libc.c`) — a one-`int`-field private
  struct behind a `typedef struct cb_libc_file FILE;` in `stdio.h`, forward
  declared as an incomplete type in `include/cannedbsd/libc.h` — is the
  established opaque-handle pattern this design reuses directly for `DIR`
  (§5).

## 2. The one new node operation

One new `cb_vfs_node_ops` entry, appended after `truncate` (wherever that
currently is at implementation time — append-only, per `AGENTS.md`):

```c
/* Optional extension; callers must check struct_size before reading. */
int (*child_at)(struct cb_vfs_node *directory, size_t index,
                struct cb_vfs_node **child_out);
```

- `directory` must be a `CB_NODE_DIRECTORY` (checked by the caller via
  `stat`, mirroring how `cb_vfs_truncate_path` checks `status.type` before
  ever consulting `ops->truncate`, `src/vfs.c:335`-`338`).
- Returns `0` and `*child_out = NULL` for "no entry at this index — clean
  end of directory," and a negative `-CB_E*` value for a real error.
  **This is the EOF-versus-error rule the accept criteria names**: end of
  directory is not an error and must never be reported as one; a real
  error (e.g. a future mount's I/O failure) is a distinct outcome the
  ordinary-facing `readdir` (§5) must translate into a nonzero `errno`.
- `index` is an ordinal position (0, 1, 2, ...), not a node pointer or an
  opaque cookie. This is the one substantive design choice this document
  makes, and it is made instead of two more-tempting alternatives:

  - **Rejected: retain the "current" child node and advance via its own
    `next_sibling`.** This looks natural (it mirrors how `find_child`
    already walks the list, `src/ramfs.c:158`), but it runs straight into
    the hazard in §1: if the node the cursor is currently sitting on gets
    unlinked, `ramfs_unlink` clears *that node's own* `next_sibling` to
    `NULL`, so the next `readdir()` call would see a false, premature end
    of directory — silently dropping every entry that came after the
    unlinked one in list order, even though they are all still there.
    Making this safe would require also changing `ramfs_unlink` to
    preserve `next_sibling` while any external reference remains — a
    genuine, cross-cutting change to already-shipped, reviewed unlink
    behavior, for a feature that does not need it.
  - **Rejected: a mount-supplied opaque cursor token
    (`void *cursor`, created/advanced/destroyed by three separate ops).**
    More general, and closer to what a future persistent or rump-FS mount
    might eventually want, but it is a bigger ABI surface (three ops and a
    lifetime contract for an opaque pointer) for a v0.1 need that RAMFS's
    actual representation does not require. Revisit only if and when a
    second mount type demonstrates it cannot implement `child_at`
    reasonably.
  - **Chosen: ordinal index, re-walked from `directory->children` on every
    call.** RAMFS's `child_at` is `find_child`'s sibling: walk from the
    head, counting, return the node at position `index`. No new
    per-directory or per-node mutable state, no change to `ramfs_unlink`,
    no opaque-pointer lifetime to manage. Cost is O(n) per call
    (O(n²) for a full enumeration) against RAMFS's in-memory list; for
    v0.1's small directories this is the right trade for the size and risk
    reduction. If a future mount's directory representation makes that
    unacceptable, that mount can still implement `child_at` correctly (just
    less cheaply, or by caching); the *interface* does not force an O(n²)
    implementation, only this one's current backing structure does.

  The trade this choice makes under concurrent mutation, stated precisely
  rather than left as a vague "unspecified" (POSIX explicitly permits
  either failure mode for `readdir()` racing a `mkdir`/`unlink` in the same
  directory — this design just picks one and tests it, per §7): if an
  already-returned entry is removed before the next call, every entry that
  existed *after* it in list order shifts one position earlier, so the very
  next `readdir()` silently skips what was the following entry (it now
  occupies the already-consumed index) rather than ever returning it twice
  or crashing. Entries added after `opendir()` may or may not appear,
  depending on whether they land before or after the current index. No
  entry is ever returned as a dangling/use-after-free reference, because
  `child_at` always re-derives the pointer fresh from the live list on
  every call — nothing is cached across calls except the plain integer
  `index`.

## 3. Why the index approach also solves cross-task isolation for free

A getopt-shaped worry — "does this need task-local state the way `PENV-01`/
`PENV-03` needed it?" — does not apply here, and it is worth saying why
explicitly rather than silently assuming it: `errno`/`environ`/getopt state
are *singleton* task-global values (one `errno` per task, full stop), so
sharing them across cooperatively-interleaved tasks was the entire bug
class those iterations fixed. A `DIR*` is not a singleton — it is an
ordinarily-`malloc`'d object, one per `opendir()` call, and its cursor
(§5: a plain `size_t index`) lives *inside that allocation*, not in any
shared per-task or per-node location. Two tasks independently iterating
the same directory, or one task calling `opendir()` twice on the same
path, each get their own independent `DIR*` with their own independent
`index`; nothing in `child_at`'s design reads or writes any state shared
between them. Isolation here is a consequence of ordinary heap-allocation
ownership (the same reasoning that already makes two independently-`open()`ed
regular-file descriptors on the same path correctly independent, per
`SPEC.md` §7), not a new mechanism this design has to invent. §7's
independent-cursor test exists to *demonstrate* this, not to make it true.

## 4. Directory lifetime across unlink

Per §1, the directory node itself cannot be removed in v0.1 (no working
`rmdir`), so the only lifetime question that can actually arise is: **what
does an active iterator observe when a file inside the directory it is
iterating gets unlinked?** Answer, following directly from the chosen
`child_at` design (§2): the removed node stops appearing (its slot is
filled by whatever used to be the next entry, per the shift described
above); no crash, no stale name, no error. `opendir()` itself retains the
directory node (via the existing generic `retain`/`release` ops, exactly
like `cb_vfs_open` retains a regular file's node) purely so the directory
itself cannot be freed while an iterator holds it open — again, the same
lifetime rule `SPEC.md` §7 already establishes for open regular files,
extended to directories with no new concept.

## 5. Ordinary-facing interface: opaque `DIR`, `struct dirent`, three functions

New `libc/include/dirent.h`, modeled directly on `stdio.h`'s `FILE`
pattern:

```c
#ifndef CANNEDBSD_DIRENT_H
#define CANNEDBSD_DIRENT_H

#include "cannedbsd/libc.h"

#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_DIR 2

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[256];
};

typedef struct cb_libc_dir DIR;

DIR *cb_libc_opendir(const char *path);
struct dirent *cb_libc_readdir(DIR *dirp);
int cb_libc_closedir(DIR *dirp);

#define opendir cb_libc_opendir
#define readdir cb_libc_readdir
#define closedir cb_libc_closedir

#endif
```

- `d_name`'s fixed `256`-byte bound is deliberate: it lets `struct dirent`
  be a plain, fully-owned-by-the-caller-or-`DIR`-object value (no separate
  allocation per name, matching this project's already-established
  "bounded, not general" posture from `PENV-04`'s formatter) and matches
  `CB_PATH_MAX`'s existing order of magnitude
  (`include/cannedbsd/abi.h`, `CB_PATH_MAX = 1024`, of which a single path
  *component* is necessarily far shorter). A name that cannot fit is a
  construction error in RAMFS's own `ramfs_create`, which already rejects
  it at creation time (`name == NULL || name[0] == '\0'`, `src/ramfs.c:196`
  — extending that existing check to a length bound, if one does not
  already exist by the time this is implemented, is a one-line, in-scope
  adjacent fix worth confirming, not a new subsystem).
- `ino_t`: add `typedef uint64_t ino_t;` to the existing
  `libc/include/sys/types.h` (introduced by `FS-01` for `off_t`) — matching
  `cb_stat_v1.inode`'s already-`uint64_t` type exactly, no narrowing.
- Private representation, `libc/cb_libc.c` (or a new `libc/cb_libc_dir.c` if
  the file is getting large by then — an implementation-time call, not a
  design one):

  ```c
  struct cb_libc_dir {
      struct cb_vfs_node *node;   /* opaque to ordinary source; retained */
      size_t index;
      struct dirent entry;        /* reused every readdir() call, like FILE* */
  };
  ```

  `readdir()` reuses one `struct dirent` per `DIR*` across calls — matching
  real POSIX `readdir()`'s own documented contract (the returned pointer is
  invalidated by the next call on the *same* stream, but distinct streams
  never interfere, consistent with §3).
- `opendir`, `readdir`, `closedir` need their own new-ABI counterparts —
  most plausibly three new `cb_api_v1` operations
  (`opendir`/`readdir`/`closedir`-shaped, taking a path or an opaque handle
  at the ABI boundary) mirroring `open`/`read`/`close`'s existing shape, OR
  (smaller ABI surface) the libc layer can call the *existing*
  `cb_vfs_node` machinery directly if `include/cannedbsd/libc.h` is allowed
  to reach `struct cb_vfs_node` — **it currently is not** ("The veneer...
  uses only `include/cannedbsd/abi.h`; it cannot include runtime-private
  types," `LIBC.md` §"Layer boundary"). So this does need genuine new ABI
  surface, not just a libc-level wrapper over existing operations the way
  `errx` reused `exit`+the formatter. Recommend three ops on `cb_api_v1`
  (append-only, after whatever is last at implementation time):

  ```c
  int (*opendir)(const char *path, void **handle_out);
  int (*readdir)(void *handle, char *name_out, size_t name_size,
                 uint64_t *inode_out, uint32_t *type_out);
  int (*closedir)(void *handle);
  ```

  `readdir`'s ABI shape returns `0` with an empty `*name_out` (`'\0'` at
  index 0) for clean end-of-directory, matching `child_at`'s own
  EOF-versus-error convention one layer up, and keeping `struct dirent`
  and the opaque `DIR` entirely a libc-side concept — the ABI never
  exposes `struct cb_vfs_node` or the internal `size_t index` cursor
  representation, preserving the same boundary `truncate`/`ftruncate`
  already established for `FS-01`.
- `opendir` failure paths (path does not resolve, resolves to a
  non-directory, or the underlying node's `ops` table is too old/absent
  for `child_at`) must translate to `ENOTDIR`/`ENOENT`/`ENOSYS`
  respectively, `NULL` return, no partial allocation retained — see §7 for
  the specific atomicity test.

## 6. Mutation policy, stated as one sentence for the record

`readdir()` reflects the directory's live membership at the moment of each
call; an entry already returned is never returned again, but a
concurrent removal of that specific entry may cause the single following
entry to be skipped, and a concurrent addition may or may not be
observed, depending on its position relative to the current index — this
is within POSIX's own explicitly-permitted looseness for `readdir()` under
concurrent modification, and this design picks one concrete, deterministic
(for a fixed sequence of calls) behavior rather than leaving it
implementation-varying, so it can be tested precisely (§7).

## 7. Falsifiable test matrix

Following the established `PENV`/`FS-01` pattern: an ordinary-source
`tests/libc_dirent_probe.c` (no cannedBSD names) referencing
`opendir`/`readdir`/`closedir`, linked into `build/test_core`, gives the
required red — compiles clean once the headers/ABI declarations exist,
fails to **link** until the implementation exists (matching
`PENV-02`/`PENV-03`/`PENV-05`'s literal "fails to link" reds, and this
backlog item's own "fails to compile" wording in the same spirit: the
interface is declared as part of establishing red, only the definition is
missing).

Raw-ABI and ordinary-source cases to cover:

1. **Root enumeration**: `opendir("/")` returns entries for `bin`, `tmp`,
   `home` (the fixed initial tree, `SPEC.md` §7) — order not asserted
   (§2's list order is not a promised contract), *set* of names and types
   is.
2. **EOF versus error**: exhausting a real directory returns `NULL` with
   `errno` left at `0` (or whatever it was before the call — `readdir`
   must not touch `errno` on clean EOF); a forced error path (an
   `opendir()` on a node whose mount supplies a deliberately
   too-small/absent `child_at`, exercising the `ENOSYS` guard from §2)
   returns `NULL` with `errno` set — the two `NULL` returns must be
   distinguishable by the caller exactly the way real code distinguishes
   them (check `errno` after a `NULL`).
3. **Independent cursors**: two `opendir()` calls on the same path (from
   one task, and — forcing genuine cooperative interleaving via the
   established past-pipe-capacity blocking mechanism, `PENV-01`'s
   `environprobe` pattern — from two *different* tasks) each read the
   complete, independent entry set; advancing one never perturbs the
   other's `index`.
4. **Mutation policy, exactly as stated in §6**: `opendir` a directory with
   three known entries, `readdir` once (consume entry N), unlink the
   *next* not-yet-returned entry from a position after the cursor,
   `readdir` again, and assert the specific, predicted skip — not merely
   "doesn't crash."
5. **Allocation failure**: inject a `cb_allocate` failure for the `DIR`
   object itself (reusing the existing controlled-allocator test-harness
   hooks already used for `realloc`/pipe-construction atomicity elsewhere)
   and assert `opendir` returns `NULL`, `errno == ENOMEM`, and the
   directory node's reference count is back to its pre-call value (no
   retained-but-orphaned node).
6. **Cleanup**: `closedir` releases the retained node
   (`cb_test_task_allocation_count`-style introspection, or a
   reference-count-visible test hook if one does not already exist);
   a task that exits holding an open, never-`closedir`'d `DIR*` has it
   swept by the existing task-allocation-reclaim path
   (`task_release_allocations`) exactly like an unfreed `malloc`, requiring
   no new cleanup mechanism.
7. **Non-directory and old-table rejection**: `opendir()` on a regular
   file returns `ENOTDIR`; `opendir()` on a node whose `ops->struct_size`
   predates `child_at` returns `ENOSYS`, both with no partial state.

## 8. Exact files (implementation checklist for whoever picks this up)

| File | Change |
|---|---|
| `include/cannedbsd/abi.h` | Append `opendir`/`readdir`/`closedir` to `cb_api_v1` |
| `src/internal.h` | Append `child_at` to `cb_vfs_node_ops` (after `truncate`) |
| `src/vfs.c` | Dispatch for the three new ABI ops, `struct_size`-guarded exactly like `cb_vfs_truncate`/`cb_vfs_ftruncate` (`src/vfs.c:339`-`343`) |
| `src/ramfs.c` | Implement `ramfs_node_ops.child_at` (walk-and-count from `directory->children`, reusing `find_child`'s traversal shape) |
| `src/core.c` | Wire `api->opendir`/`readdir`/`closedir` in `initialize_api`; extend `api_is_usable`-equivalent (`abiprobe`) non-`NULL` checks |
| `libc/include/sys/types.h` | Add `typedef uint64_t ino_t;` alongside the existing `off_t` |
| `libc/include/dirent.h` (new) | `DIR`, `struct dirent`, `opendir`/`readdir`/`closedir` macros |
| `include/cannedbsd/libc.h` | Declare `cb_libc_opendir`/`readdir`/`closedir`; forward-declare `struct cb_libc_dir` |
| `libc/cb_libc.c` | Define the three wrappers and `struct cb_libc_dir` |
| `tests/libc_dirent_probe.c` (new) | Ordinary-source red/green probe |
| `tests/libc_dirent_probe_module.c` (new) | `CB_LIBC_PROGRAM` adapter, if the probe needs to run as a registered command rather than purely through raw-ABI orchestration |
| `Makefile` | Wire the new probe object, matching the `libc_exit_probe`/`libc_getopt_probe`/`libc_errx_probe` precedent |
| `tests/test_libc_source.sh` | Boundary check: probe uses no cannedBSD names, object imports `cb_libc_opendir`/`readdir`/`closedir` not raw host names |
| `tests/test_core.c` | The raw-ABI orchestrator tests in §7; extend `abiprobe` |
| `SPEC.md` §4.5/§7 | Document the new node op and the ordinary `dirent` surface |
| `LIBC.md` | New `dirent.h` bullet |
| `notes/iterations/VFS-03.md` (new, at implementation time) | Red/green/Woodpecker evidence, per `AGENTS.md` |

No changes needed in `src/shell.c`, `src/executor.c`, `src/programs.c`, or
`commands/` — this is VFS/libc surface only, matching the backlog's own
scope statement. `platform/mac68k/CMakeLists.txt` needs no change either
(it lists individual translation units explicitly, as `PENV-06` found the
hard way for a *command*; this design adds no new command, only library
and core-runtime files already covered by that build's existing
`${ROOT}/src/core.c`/`libc/cb_libc.c` entries).

## 9. Explicitly not claimed

`readdir_r` (obsolete, never worth adding), `rewinddir`/`seekdir`/`telldir`
(no consumer yet; `index` happens to make `rewinddir` trivial —
`dirp->index = 0` — but that is not part of this proposal until something
needs it), `scandir`/`alphasort`, symlinks-in-directories (deferred
project-wide, `SPEC.md` §7), and directory removal/`rmdir` (blocked on
`ramfs_unlink`'s own directory-removal gap, §1 — a prerequisite for a
*different* backlog item, not this one).
