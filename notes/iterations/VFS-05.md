# VFS-05: directory-iteration cursor survives self-mutation through the same handle

- **Status:** implemented, `make ci` pending final confirmation.
- **Origin:** found while implementing `RM-01`'s `rm -r`, which silently
  failed to remove entire subtrees of any directory with more than one
  entry. Root-caused, documented, and deliberately **not** fixed under
  `RM-01`'s own ID, since it meant reopening an explicitly reasoned,
  already-accepted design decision (`VFS-03`'s `child_at()` ordinal
  index) rather than fixing an oversight in code that ID owned. Given its
  own ID by the coordinator once the diagnosis was confirmed correct.
- See `notes/iterations/VFS-03.md`'s new addendum for the precise
  relationship to the original design: an extension of its concurrency
  reasoning, not a reversal. That reasoning was sound for a generic
  external mutator racing an iterator; it was silent about the same task
  self-mutating through the same handle, which is `rm -r`'s (and any
  future recursive `cp`) entire access pattern, not an occasional race.

## The bug, precisely

`ramfs_child_at(directory, index, &child_out)` re-walks
`directory->children` from the head on every call, returning whatever
node currently sits at ordinal position `index`. `struct cb_dir_handle`
tracked a plain incrementing `index`. For a directory `[top, a]` (RAMFS
prepends, so `top` — created after `a` — is first): `readdir()` returns
`top` at index 0; the caller unlinks `top`, shrinking the list to `[a]`;
the next `readdir()` advances to index 1, which no longer exists, falsely
reporting end-of-directory. `a` (and its entire subtree) is never visited
or removed, and a subsequent `rmdir()` on the parent correctly reports
`ENOTEMPTY` for the directory it never finished processing.

## The fix: look ahead before returning, not lazily on the next call

`VFS-03`'s design explicitly considered and rejected retaining a cursor
node and advancing via its own `next_sibling` on the *following* call,
because `ramfs_unlink` clears a removed node's own `next_sibling` to
`NULL` — reading it after the node might already be gone gives a false
premature end. This fix avoids exactly that hazard by never reading
`next_sibling` after the fact: it captures the current entry's own
next-sibling identity **in the same `readdir()` call that returns the
current entry**, before control ever returns to a caller who might then
unlink it.

New node op (`src/internal.h`, appended after `rename`, `struct_size`-
guarded like every other optional extension since `truncate`):

```c
int (*next_sibling)(struct cb_vfs_node *node,
                    struct cb_vfs_node **sibling_out);
```

RAMFS's implementation (`src/ramfs.c`) is a direct field read — `struct
cb_ramfs_node` already has a `next_sibling` pointer for exactly this
relationship, unlike `child_at`, which has to walk from the head because
nothing generic exposes list position by node identity. Generic wrapper
`cb_vfs_next_sibling` in `src/vfs.c` mirrors `cb_vfs_child_at`'s own
guard shape exactly (`-CB_EIO` for an invalid node, `-CB_ENOSYS` for an
old/absent table).

`struct cb_dir_handle` (`src/internal.h`) now holds `struct cb_vfs_node
*next` — the entry the *next* `readdir()` call will return, identified
by live, retained node pointer — instead of `size_t index`. `api_opendir`
primes it via `cb_vfs_child_at(node, 0, &first)` (unaffected by this
change: nothing has mutated yet at open time). `api_readdir` returns
`handle->next`, then, only once committed to a successful return (after
the name-length and stat checks that can still fail and leave the same
entry retriable), looks up that entry's own `next_sibling`, retains it if
non-`NULL`, releases the entry being returned, and stores the looked-
ahead node as the new `handle->next`. `api_closedir` releases
`handle->next` (if still non-`NULL`, i.e. the directory was closed before
being drained) in addition to releasing `handle->node` as before.

## A second correctness property fixed as a direct consequence, not separately targeted

`VFS-03`'s design also explicitly accepted a duplicate-on-insertion case
(a concurrent `mkdir`/`create` prepending a new node shifts an
already-visited entry back into an as-yet-unvisited ordinal position).
Because the new cursor advances by a specific node's own identity rather
than a position recomputed from the list's current shape, an unrelated
insertion elsewhere in the list cannot perturb it — the fix for the skip
case removes the duplicate case for the same underlying reason, not via
a second, separate mechanism.

## Testing

`tests/test_core.c`'s `direntmutationprobe` (item 4a/4b from `VFS-03`)
had its own assertions updated: it previously asserted the skip and the
duplicate as the *correct* outcomes (`VFS-03`'s own accepted behavior at
the time); it now asserts neither occurs, with an explicit comment
explaining why the expectations changed rather than silently flipping
them. A new `direntdrainprobe` proves the actual motivating pattern
directly: a 5-entry directory, visited and unlinked one entry at a time
through a single open handle, asserting every entry is visited exactly
once (no skip, no duplicate, order-independent). No new allocation is
introduced by this fix (`retain`/`release` are refcount operations on
already-existing nodes, not new allocations), so no allocation-failure
injection test was needed the way `VFS-04`'s rename fix needed one.

## Evidence

`make LDLIBS=-lucontext test` and `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
both green in the `alpine:3.22` container, including the sanitize
(ASan/UBSan) rebuild — relevant here specifically because a leaked or
double-released retained node under the new cursor's early-`closedir()`
path would be exactly the kind of defect LeakSanitizer/ASan catches, and
the existing test suite already exercises `opendir`/`readdir`/`closedir`
extensively beyond this iteration's own new tests. `MV-01`'s own
behavioral matrix (already merged) also passed unchanged against this
fix. Mac68k guest acceptance: **PENDING**, not run this iteration, per
standing direction for this batch of work.
