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

  **Correction from an earlier draft of this document**: that draft claimed
  "an entry already returned is never returned again" under this scheme.
  That claim is false, and a concrete counterexample proves it:
  `node_create` always prepends (`src/ramfs.c:102`,
  `node->next_sibling = parent->children; parent->children = node;`), so
  given directory `[A, B]`, `readdir()` returning `A` at index `0`,
  a concurrent create of `X` produces `[X, A, B]` — the *next* `readdir()`
  call, advancing to index `1`, now finds `A` again (`X` occupies the slot
  `A` used to be at, and `A` has shifted into the slot the cursor is about
  to visit). This is the mirror image of the already-documented removal
  case: removal-after-cursor shifts later entries *earlier*, silently
  skipping one; insertion-at-head shifts already-visited entries *later* by
  one, silently duplicating one. Both are real, both are cheap to trigger,
  and index-based traversal cannot avoid either one without either
  changing `ramfs_unlink` (rejected in the previous bullet) or adopting a
  heavier opaque-cursor mechanism (also rejected above). Given the choice
  the reviewer of this document posed — allow and test duplicates under
  mutation, or choose different, stable iteration semantics — this design
  takes the first option: **both a duplicate-on-concurrent-insert and a
  skip-on-concurrent-removal are explicitly permitted, deterministic for a
  fixed sequence of calls, and directly tested** (§7 items 4a/4b), rather
  than pretending one of them cannot happen. This is squarely within what
  POSIX already permits `readdir()` to do when racing a `mkdir`/`unlink` in
  the same directory; the value added here is stating the *exact* shape of
  each case instead of leaving it an unstated implementation accident. No
  entry is ever returned as a dangling/use-after-free reference either way,
  because `child_at` always re-derives the pointer fresh from the live list
  on every call — nothing is cached across calls except the plain integer
  `index`.

## 3. Why the index approach also solves cross-task isolation for free

A getopt-shaped worry — "does this need task-local state the way `PENV-01`/
`PENV-03` needed it?" — does not apply here, and it is worth saying why
explicitly rather than silently assuming it: `errno`/`environ`/getopt state
are *singleton* task-global values (one `errno` per task, full stop), so
sharing them across cooperatively-interleaved tasks was the entire bug
class those iterations fixed. A directory descriptor is not a
singleton — **correcting an earlier draft's framing**, its cursor does
not live in a `DIR*`'s own `malloc`ed allocation at all; it lives in one
slot of the per-task `directories[CB_MAX_DIRS]` table (§5), the same
shape as the already-existing `descriptors[CB_MAX_FDS]`. Two tasks
independently iterating the same directory, or one task calling
`opendir()` twice on the same path, each get their own slot with their
own independent `index`; nothing in `child_at`'s design reads or writes
any state shared between them, and every dispatch function resolves a
directory descriptor only against `active_kernel->current`'s own table
(§5), so there is no code path through which one task's descriptor
number could even be misdirected at another task's slot. Isolation here
is a direct consequence of that per-task table shape (the same reasoning
that already makes two independently-`open()`ed regular-file descriptors
on the same path correctly independent, per `SPEC.md` §7), not a new
mechanism this design has to invent. §7's independent-cursor test exists
to *demonstrate* this, not to make it true.

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
    char d_name[CB_PATH_MAX];
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

- **Correction from an earlier draft of this document**: `d_name` is sized
  to the *existing* `CB_PATH_MAX` (`include/cannedbsd/abi.h`, `= 1024`), not
  an arbitrary smaller bound. `ramfs_create` (`src/ramfs.c:196`) rejects a
  `NULL`, empty, or `/`-containing name today, but **has no length check at
  all** — a component up to just under `CB_PATH_MAX` bytes can already be
  created successfully (the only practical ceiling today is
  `normalize_for_task`'s own `char normalized[CB_PATH_MAX];` stack buffer
  for the *whole* path, `src/vfs.c`). An earlier draft of this design
  proposed a smaller, invented `256`-byte `d_name` plus "extend
  `ramfs_create` with a length check" — that would have been a real
  behavior regression, silently rejecting creates that succeed today. The
  fix belongs entirely on the read side: size `d_name` to the bound that
  already, provably exists (`CB_PATH_MAX`), so no name RAMFS can ever
  actually produce can overflow it, and no creation-time behavior changes
  at all. This does make `struct dirent` a chunkier value
  (`CB_PATH_MAX + 8 + 1` bytes, reused once per `DIR*`, not once per
  entry) — an acceptable, deliberate trade against inventing a new
  overflow-handling error path for a case that cannot occur.
- `ino_t`: add `typedef uint64_t ino_t;` to the existing
  `libc/include/sys/types.h` (introduced by `FS-01` for `off_t`) — matching
  `cb_stat_v1.inode`'s already-`uint64_t` type exactly, no narrowing.
- **Correction from an earlier draft of this document**: the earlier
  private-representation sketch had `struct cb_libc_dir` hold a
  `struct cb_vfs_node *` directly — a runtime-private type that
  `include/cannedbsd/libc.h`/`libc/cb_libc.c` are not permitted to name at
  all ("The veneer... uses only `include/cannedbsd/abi.h`; it cannot
  include runtime-private types," `LIBC.md` §"Layer boundary" — the same
  draft even *said* this a few lines later while contradicting it here).
  Fixed below: the libc struct holds only the opaque handle the ABI hands
  back, exactly like `struct cb_libc_file` holds only a plain `int`
  descriptor (`libc/cb_libc.c`), never a runtime object.
- **New finding, not in the earlier draft: where the retained node
  actually lives.** `task_release_allocations` (`src/core.c`) only walks
  and frees the task's raw `cb_allocate`d memory blocks — it has no way to
  know that one of those blocks happens to embed a `struct cb_vfs_node *`
  that also needs `ops->release()` called on it. Relying on it (as the
  earlier draft's §7 cleanup item did) would leak the VFS node's reference
  count forever on any task that exits without an explicit `closedir()` —
  a real bug, not a documentation gap. Directory handles need the same
  explicit, task-owned tracking descriptors already get
  (`struct cb_fd_entry descriptors[CB_MAX_FDS]`, `src/internal.h`), not
  generic allocation sweeping. Proposed addition to `struct cb_task`
  (`src/internal.h`), a fixed-size table mirroring `descriptors[]`:

  ```c
  #define CB_MAX_DIRS 16  /* directories held open concurrently are rare;
                              no dynamic growth needed for v0.1 */

  struct cb_dir_handle {
      struct cb_vfs_node *node;  /* retained while in_use */
      size_t index;
      int in_use;
  };
  ```

  ...and `struct cb_dir_handle directories[CB_MAX_DIRS];` on `struct
  cb_task` itself. `opendir`'s ABI operation (below) finds a free slot,
  retains the resolved directory node into it, and returns the slot's
  index as a plain `int` "directory descriptor" — deliberately not a
  `void *` opaque pointer: this project already has exactly one precedent
  for "a small integer naming a per-task open resource," the file
  descriptor, and a directory descriptor should look and validate the same
  way, not invent a second style. `readdir`/`closedir` take that `int` and
  validate it exactly like `api_read`/`api_close` already validate an fd
  (`descriptor >= 0 && descriptor < CB_MAX_FDS && ...`, `src/core.c`):
  bounds-check against `CB_MAX_DIRS` and check `in_use`. **Ownership
  validation is then free by construction, not an extra check to
  remember**: every one of these dispatch functions indexes
  `active_kernel->current->directories[...]` — the *currently running*
  task's own table — so there is no code path through which a directory
  descriptor number could ever be resolved against a different task's
  table, exactly like today's fd dispatch. A new `dir_close_all(task)`
  (mirroring `fd_close_all`, `src/core.c`) walks `directories[]` and
  releases every `in_use` node; it is called from the same two places
  `fd_close_all` already is: `api_exit` (task exit) and
  `task_finish_exec` (successful exec) — so **explicit close, task exit,
  and exec are the three cleanup paths**, mirroring descriptor cleanup
  exactly, and a directory descriptor does **not** survive `exec`
  (there is no directory-handle equivalent of close-on-exec being
  clearable — every open directory closes on exec unconditionally, the
  simplest safe default, since nothing in this project needs one to
  survive it).
- Private libc-level representation, `libc/cb_libc.c` (or a new
  `libc/cb_libc_dir.c` if the file is getting large by then — an
  implementation-time call, not a design one):

  ```c
  struct cb_libc_dir {
      int descriptor;       /* the opaque handle the ABI returned; opaque
                                to ordinary source, meaningless outside
                                the runtime that issued it */
      struct dirent entry;  /* reused every readdir() call, like FILE* */
  };
  ```

  `readdir()` reuses one `struct dirent` per `DIR*` across calls — matching
  real POSIX `readdir()`'s own documented contract (the returned pointer is
  invalidated by the next call on the *same* stream, but distinct streams
  never interfere, consistent with §3). Note `struct cb_libc_dir` no longer
  needs its own `index` at all — the cursor position lives entirely in the
  runtime's `struct cb_dir_handle.index` (above), reachable only through
  the opaque `descriptor`; the libc layer is a pure pass-through, exactly
  like `struct cb_libc_file` is for regular descriptors.
- `opendir`, `readdir`, `closedir` need their own new-ABI counterparts —
  genuine new ABI surface, not just a libc-level wrapper over existing
  operations the way `errx` reused `exit`+the formatter, because
  `include/cannedbsd/libc.h` cannot reach `struct cb_vfs_node` (above).
  Three ops on `cb_api_v1` (append-only, after whatever is last at
  implementation time):

  ```c
  int (*opendir)(const char *path);
  int (*readdir)(int descriptor, char *name_out, size_t name_size,
                 uint64_t *inode_out, uint32_t *type_out);
  int (*closedir)(int descriptor);
  ```

  `opendir` returns the new directory descriptor (`>= 0`) or `-1` with the
  task's errno cell set. `readdir`'s ABI shape returns `0` with an empty
  `*name_out` (`'\0'` at index 0) for clean end-of-directory, matching
  `child_at`'s own EOF-versus-error convention one layer up, and keeping
  `struct dirent` and the opaque `DIR` entirely a libc-side concept — the
  ABI never exposes `struct cb_vfs_node` or the internal `size_t index`
  cursor representation, preserving the same boundary `truncate`/
  `ftruncate` already established for `FS-01`.
- `opendir` failure paths (path does not resolve, resolves to a
  non-directory, or the underlying node's `ops` table is too old/absent
  for `child_at`) must translate to `ENOTDIR`/`ENOENT`/`ENOSYS`
  respectively, `NULL` return, no partial allocation retained — see §7 for
  the specific atomicity test.

## 6. Mutation policy, stated as one sentence for the record

`readdir()` re-derives the entry at the current ordinal position from the
directory's live membership on every call: a concurrent removal of an
already-returned entry shifts every later entry one position earlier,
causing the very next call to silently **skip** the entry that used to
follow it; a concurrent creation (which `node_create` always inserts at
the head) shifts every already-visited entry one position later, causing
the very next call to silently **re-return** (duplicate) the entry most
recently returned. Both outcomes are deterministic for a fixed sequence of
calls, both are within POSIX's own explicitly-permitted looseness for
`readdir()` racing directory modification, and both are directly tested
(§7) rather than left as an unstated implementation accident.

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
4. **Mutation policy, exactly as stated in §6** — both directions, not one:
   - **4a, skip on removal**: `opendir` a directory with three known
     entries, `readdir` once (consume entry N), unlink the *next*
     not-yet-returned entry from a position after the cursor, `readdir`
     again, and assert the specific, predicted skip — not merely "doesn't
     crash."
   - **4b, duplicate on insertion**: `opendir` a directory with two known
     entries, `readdir` once (consume entry `A` at index `0`), create a
     new entry (landing at the head, per `node_create`'s prepend
     behavior), `readdir` again, and assert the specific, predicted
     duplicate — the next call returns `A` a second time, not the second
     original entry or the new one.
5. **Allocation failure**: inject a `cb_allocate` failure for the free-slot
   search in `opendir` (reusing the existing controlled-allocator
   test-harness hooks already used for `realloc`/pipe-construction
   atomicity elsewhere — note `opendir` itself does not need a new heap
   allocation if `struct cb_dir_handle` lives inline in a fixed-size
   `struct cb_task` array (§5); the allocation this test targets is
   whatever the *libc-level* `cb_libc_opendir` needs for its own
   `struct cb_libc_dir`) and assert `opendir` returns `NULL`, `errno ==
   ENOMEM`, and the directory node's reference count is back to its
   pre-call value (no retained-but-orphaned node, no `in_use` slot left
   set with no corresponding live `DIR*`).
6. **Cleanup, all three paths**: `closedir` releases the retained node and
   clears `in_use` (directly observable via the node's own reference
   count, or a new `cb_test_...`-style introspection hook mirroring
   `cb_test_task_allocation_count` if one does not already exist for
   `directories[]`); a task that `exit`s holding an open, never-`closedir`'d
   directory descriptor has it released by `dir_close_all` from `api_exit`,
   exactly like an un-`close()`d file descriptor is by `fd_close_all` —
   *not* by `task_release_allocations`, which cannot see into a retained
   VFS node at all (§5's ownership finding); a task that successfully
   `exec`s has every open directory descriptor closed unconditionally
   (§5 — there is no close-on-exec flag to preserve, since none is
   proposed).
7. **Non-directory and old-table rejection**: `opendir()` on a regular
   file returns `ENOTDIR`; `opendir()` on a node whose `ops->struct_size`
   predates `child_at` returns `ENOSYS`, both with no partial state (no
   `directories[]` slot left `in_use`).
8. **Descriptor-table exhaustion and validation**: the
   `(CB_MAX_DIRS + 1)`th concurrent `opendir()` fails with `EMFILE` (or
   equivalent), mirroring `fd_install`'s existing `CB_MAX_FDS` exhaustion
   behavior (`src/core.c`); an out-of-range or not-`in_use` directory
   descriptor passed to `readdir`/`closedir` fails with `EBADF`, mirroring
   `api_read`/`api_close`'s existing descriptor validation exactly.

## 8. Exact files (implementation checklist for whoever picks this up)

| File | Change |
|---|---|
| `include/cannedbsd/abi.h` | Append `opendir`/`readdir`/`closedir` to `cb_api_v1` |
| `src/internal.h` | Append `child_at` to `cb_vfs_node_ops` (after `truncate`); add `struct cb_dir_handle` and `directories[CB_MAX_DIRS]` to `struct cb_task` (§5) |
| `src/vfs.c` | Dispatch for the three new ABI ops, `struct_size`-guarded exactly like `cb_vfs_truncate`/`cb_vfs_ftruncate` (`src/vfs.c:339`-`343`) for the `child_at` check specifically |
| `src/ramfs.c` | Implement `ramfs_node_ops.child_at` (walk-and-count from `directory->children`, reusing `find_child`'s traversal shape) |
| `src/core.c` | Implement `api_opendir`/`api_readdir`/`api_closedir` with fd-shaped validation against `directories[]` (mirroring `api_read`/`api_close`'s existing `descriptor`-bounds-and-liveness checks); new `dir_close_all(task)` mirroring `fd_close_all`, called from both `api_exit` and `task_finish_exec`; wire `api->opendir`/`readdir`/`closedir` in `initialize_api`; extend `abiprobe`'s non-`NULL` checks |
| `libc/include/sys/types.h` | Add `typedef uint64_t ino_t;` alongside the existing `off_t` |
| `libc/include/dirent.h` (new) | `DIR`, `struct dirent`, `opendir`/`readdir`/`closedir` macros |
| `include/cannedbsd/libc.h` | Declare `cb_libc_opendir`/`readdir`/`closedir`; forward-declare `struct cb_libc_dir` (holds only the opaque `int` descriptor, §5 — never `struct cb_vfs_node`) |
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
(no consumer yet; the ordinal `index` design happens to make `rewinddir`
trivial — reset `directories[descriptor].index` to `0` — but that is not
part of this proposal until something needs it), `scandir`/`alphasort`,
symlinks-in-directories (deferred
project-wide, `SPEC.md` §7), and directory removal/`rmdir` (blocked on
`ramfs_unlink`'s own directory-removal gap, §1 — a prerequisite for a
*different* backlog item, not this one).
