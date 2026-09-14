# VFS-04: `rmdir` and atomic `rename` node operations

- Status: green, awaiting coordinator review and Mac68k guest acceptance
- Base SHA: `7e1fd6a` (freshly fetched `origin/main`)
- Branch: `work/VFS-04`
- Hypothesis: directory removal and atomic replacement can be expressed over
  the existing node contract without exposing filesystem representation, and
  cross-mount `rename` can return `EXDEV` using the `VFS-01` routing
  boundary. Confirmed true.

## The `rename` justification (per Ryan's explicit ask)

The rewritten backlog entry required an explicit, evidence-based call on
whether `rename` belongs in this item, since `FILEUTIL-01`'s diagnostics
never reached a `rename()` call site (`mv.c` dies at `sys/extattr.h` first).
Compile diagnostics are not the only legitimate evidence channel here:
**direct inspection of the pinned `mv.c` source itself** (re-fetched from the
same revision, hash-verified as identical to `FILEUTIL-01.md`'s recorded
`df5de897a14e2f8210140e468b94319eaf100018490bfebe564152bf337bfd54`) shows
`do_move()` calling `rename(from, to)` unconditionally at line 231, as the
first-attempted, POSIX-mandated mechanism for every same-filesystem move —
before any permission-prompt logic, and reached regardless of any `-i`/`-f`
flag. This is real, direct evidence of demand from the exact pinned file,
independent of and not overriding the (still true) fact that compile
diagnostics never reached it. `rename` is included in this item on that
basis, not inherited from the original entry's now-corrected assumption.

## Red

- Command: `make LDLIBS=-lucontext test` in the documented Alpine 3.22
  container, run against a checkout with the tests in place but the
  implementation deliberately reverted (see "Red capture method").
- Expected failure: no ordinary-source path can express directory removal or
  atomic rename today.
- Observed failure: real compile errors across exactly the intended new
  surface, not a single incidental one:
  ```
  tests/test_core.c:4957: implicit declaration of function 'cb_vfs_rmdir_path'
  tests/test_core.c:5003: 'struct cb_vfs_node_ops' has no member named 'rmdir'
  tests/test_core.c:5005: 'struct cb_vfs_node_ops' has no member named 'rename'
  tests/test_core.c:5020: implicit declaration of function 'cb_vfs_rename_paths'
  tests/test_core.c:5021: 'CB_EXDEV' undeclared
  tests/test_core.c:5137: 'const struct cb_api_v1' has no member named 'rename'
  make: *** [Makefile:379: build/test_core] Error 1
  ```
- Red capture method: same discipline as `LS-01` — the implementation
  (`include/cannedbsd/abi.h`, `libc/include/errno.h`, `src/core.c`,
  `src/internal.h`, `src/ramfs.c`, `src/vfs.c`) was written first, then
  `git checkout --`-reverted on those six files only (leaving
  `tests/test_core.c` untouched) to capture this exact red output, then
  restored via `git apply` on a saved patch. Recorded accurately per
  `AGENTS.md`: this is a reverted-and-confirmed red, not a red-first
  sequence.

## Green

- Focused/full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in the
  documented Alpine 3.22 container (host `.git/worktrees/<name>` bind-mounted
  at its original absolute path, `git config --global --add safe.directory
  '*'`) — passes clean, exit 0. Three full test-suite passes confirmed in the
  log (normal, ASan/UBSan `sanitize`, `check-build-modes`), plus
  `check-publication`, `check-architecture`, `analyze` all green.
- Linux Woodpecker / mac68k Woodpecker: not yet run (local container only;
  branch not pushed).
- Guest acceptance: **explicitly pending, per Ryan's directive that Mac
  guest acceptance is deferred right now** — not claimed as passed.

## Change and review

**Appended ABI entries only, no reordering or field changes:**
- `cb_api_v1`: appended `int (*rmdir)(const char *path)` and
  `int (*rename)(const char *old_path, const char *new_path)` after the
  existing tail (`input_state_location`).
- `cb_vfs_node_ops`: appended `int (*rmdir)(struct cb_vfs_node *node)` and
  `int (*rename)(struct cb_vfs_node *node, struct cb_vfs_node *new_parent,
  char *new_name_owned)` after the existing tail (`child_at`), matching the
  established "optional extension, struct_size-guarded" convention already
  used for `truncate`/`child_at`.
- `enum cb_error`: appended `CB_EXDEV = 18` (the real errno number), plus the
  matching `EXDEV` macro in `libc/include/errno.h` and a `strerror` case in
  `src/core.c` — required to report cross-mount rename failures at all; no
  existing errno covered it.

**`rmdir` implementation:** `ramfs_rmdir` mirrors `ramfs_unlink`'s existing
detach-from-parent-list mechanics for the empty-directory case, but as a
genuinely separate function rather than a shared helper with a bypass flag
— `ramfs_unlink` *deliberately* refuses every directory (`EISDIR`/
`ENOTEMPTY`, since `unlink()` must never remove one; `rmdir()` is the
dedicated operation for that), so reusing it here would mean punching a hole
through that existing guarantee. `cb_vfs_rmdir_path` (`src/vfs.c`) applies
the same mount-root protection `cb_vfs_unlink_path` already has (`EPERM`),
struct_size-guards the new node op (`ENOSYS` on an old table), then
delegates.

**`rename` implementation:** deliberately split so the only two
state-mutating steps (replacing an existing target, then the actual move)
are failure-free by construction once reached:
1. `cb_vfs_rename_paths` (`src/vfs.c`) resolves both paths, checks
   `old_node->mount != new_parent->mount` (`EXDEV`), rejects the source being
   a mount root (`EPERM`, mirroring `rmdir`/`unlink`), looks up whether the
   destination name already exists, and if so, `stat`s both nodes to decide
   type compatibility (`ENOTDIR` for directory-onto-file, `EISDIR` for
   file-onto-directory; directory-onto-directory is intentionally rejected
   with `ENOTEMPTY` — replacing an existing directory target is out of this
   item's tested scope, not silently supported).
2. Only once every check above has passed does it allocate the new name
   (`cb_string_duplicate`) — the one point that can fail with `ENOMEM`, and
   the *last* thing that can fail, so nothing has mutated yet if it does.
3. If replacing, unlink the existing (already type-validated) target.
4. Call `old_node->ops->rename(...)`, which takes ownership of the
   pre-allocated name and does pure pointer relinking in `ramfs_rename` —
   detach from the old parent's child list, attach to the new parent's,
   swap in the new name, free the old one. No allocation, so it cannot fail.

This ordering is what makes "leaves both names intact on failure" and "no
leak or dangling node under allocation failure injection" both true by
construction rather than by recovery logic: everything that can fail is
validate-only until the point where nothing can fail anymore.

**"Preserves open-file identity across the rename":** `ramfs_rename` moves
the *same* `struct cb_ramfs_node` (same pointer, same reference count) to a
new parent/name; it never copies data or allocates a new node. Any
`struct cb_open_file` already holding that pointer (via `object.node`)
observes the rename transparently. Tested directly: open a file, rename it,
confirm a fresh lookup at the new name returns the identical node pointer
the still-open file handle already has.

**"Atomic" is scoped honestly:** this runtime is single-threaded and
cooperative; nothing between the validation and the two final mutating
calls ever yields, so no other task can observe an intermediate state. That
is the actual atomicity guarantee here — not claimed as anything stronger
(e.g. crash-safety across a host process death, which this in-memory RAMFS
has no concept of regardless).

**Tests (`tests/test_core.c`):**
- `test_vfs_rmdir_rename()`: a direct-manipulation test (same style as the
  existing `test_vfs_mount_routing`) covering every `rmdir` and `rename`
  Accept scenario: `ENOENT`, `ENOTDIR`, `ENOTEMPTY`, mount-root `EPERM`,
  successful removal; `EXDEV`, `ENOTDIR`/`EISDIR` type mismatch (both
  directions, both proven to leave names intact), ordinary move,
  open-file-identity preservation, atomic replace of an existing
  regular-file target (proven via node-pointer identity, not just a status
  code), rename-onto-self as a no-op success, and old-`struct_size`-table
  `ENOSYS` behavior for both operations (a real truncated `cb_vfs_node_ops`
  copy swapped onto a live node, then restored and re-verified working).
- `renameallocprobe` (new `FIXTURE_VFS04`, following the exact precedent
  `FIXTURE_DIRENT`/`FIXTURE_YES` already set: `FIXTURE_FULL` is at
  `CB_MAX_PROGRAMS`'s 64-slot ceiling in this codebase's current state, so a
  test-only probe gets its own scoped fixture rather than growing that
  production constant — unlike `LS-01`'s `ls`, this isn't a base command
  every fixture needs): injects an allocation failure at the exact point
  `cb_vfs_rename_paths` calls `cb_string_duplicate`, confirms `ENOMEM`, both
  names left intact, and the runtime still works normally on a subsequent
  ordinary rename (no corrupted state). Verified leak-free under the ASan
  `sanitize` build pass.

**Documentation:** this note.

**Remaining risk or follow-up:**
- Directory-onto-directory rename replacement is intentionally unsupported
  (`ENOTEMPTY` always) — not required by Accept, but worth a design decision
  if a later item needs it rather than silently changing this behavior.
- Mac68k guest acceptance is outstanding by explicit directive, not
  oversight.
- `MV-01` can now cite this item's own direct-source-reading justification
  for `rename` rather than re-deriving it; `RM-01` needs only `rmdir` from
  here, once `FTS-01` (or a scoped-down `rm` without `-r`) exists.
