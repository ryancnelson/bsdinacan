# FTS-ERRNO-01: Clear `errno` on Clean `fts_read` Traversal Completion

- **Status:** Complete and verified; ready for standalone merge to `main`.
- **Base SHA:** `25e8e30` (`origin/main`).
- **Branch:** `work/FTS-FIX-01`.
- **Attribution:** Discovered and authored by Libby (Claude Sonnet 5 on `work/RM-01` at `c42fd4b`); independently corroborated on `work/CP-01`.

---

## 1. Defect Description

In `libc/cb_fts.c`, `cb_libc_fts_read()` returns `NULL` upon reaching the end of traversal (`ftsp->path_argv[ftsp->root_index] == NULL`). However, it previously left `errno` unchanged rather than explicitly setting `errno = 0`.

When prior benign operations (such as internal `stat` failures producing `FTS_NS`, or non-fatal metadata lookups) set `errno`, standard POSIX utilities like NetBSD `rm.c` and `cp.c` check:
```c
if ((ent = fts_read(fts)) == NULL) {
    if (errno)
        err(EXIT_FAILURE, "fts_read");
    break;
}
```
Because `errno` was not cleared on clean EOF, `rm -rf` on missing paths or `cp -r` would falsely fail with `fts_read: function not implemented` or `fts_read: No such file or directory`.

---

## 2. Fix

In `libc/cb_fts.c`, explicitly clear `errno = 0` before returning `NULL` on clean end-of-walk:
```c
        if (ftsp->path_argv[ftsp->root_index] == NULL) {
            /* Real fts_read()'s documented contract: a clean end of the
               whole walk must leave errno reflecting only a genuine
               fts-level failure, never leftover state from a per-entry
               condition (FTS_NS/FTS_DNR/etc.) this function already
               reported through fts_info/fts_errno. Callers rely on this
               directly -- rm.c's own rm_tree() checks `if (errno)
               err(1, "fts_read")` immediately after this loop ends, with
               no errno reset of its own; found via rm -rf on a missing
               path falsely exiting 1 instead of 0, not by inspection. */
            errno = 0;
            return NULL;
        }
```

---

## 3. Verification

- **Harness Test:** `tests/fts_core_walk.c` was updated to preset `errno = 17` (`EEXIST`) before the `fts_read()` loop, and explicitly assert that `errno == 0` when `fts_read()` returns `NULL` at end of walk.
- **Full CI Gate:** `make LDLIBS=-lucontext SANITIZE_CC=clang ci` runs clean with 0 errors across all stages.
