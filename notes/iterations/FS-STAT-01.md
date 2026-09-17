# FS-STAT-01: one coalesced `cb_stat_v1` metadata append

- **Status:** Done. Full `make ci` green (normal, sanitizer, isolation builds).
- **Base:** `origin/main` at `a007331` (post-`FORMAT-01`).
- **Branch:** `work/FS-STAT-01`.
- **Scope:** appended `device`, `nlink`, `uid`, `gid`, `atime_ms`, `mtime_ms`,
  `ctime_ms` to `cb_stat_v1` in a single `struct_size` increment, per
  `BACKLOG.md`'s own framing ("one append, not two" — see that entry for why
  device and timestamps/ownership are not split).

## The honest-data decisions (written down, not fabricated)

Every one of these is a real, measured capability or a real, single, already-
established constant — not an invented per-file value.

- **`device`:** a real per-mount id, assigned once at mount creation
  (`kernel->next_device`, mirroring `next_inode`'s own pattern), not a
  constant. The prior hardcoded `st_dev = 1` for every node regardless of
  mount was quietly wrong the moment a second mount existed (it would have
  made `EXDEV`/`FTS_XDEV` cross-mount detection impossible to implement
  honestly). Two mounts now genuinely report different device ids.
- **`nlink`:** computed fresh from the live children list on every `stat()`
  call, not a stored counter. RAMFS has no hardlink primitive (confirmed
  already true as of `FTS-CORE-01`), so a non-directory always has exactly
  one name pointing at it: `nlink = 1`. A directory's link count is the
  real POSIX convention — 2 (`.` plus its own entry in its parent) plus one
  for each child directory's own `..` pointing back — computed by walking
  `node->children`, not synthesized.
- **`uid`/`gid`: fixed `0`.** This is the one decision most likely to look
  like a fabricated stub, so the reasoning is explicit: this runtime has no
  user/group concept anywhere, and permissions are deferred for this whole
  milestone. `cb_libc_getuid()` already committed to "every task is uid 0"
  as this system's single-user model before this ID existed. Reporting
  `st_uid = st_gid = 0` for every node is consistent with that existing
  commitment, not a new invention — it is the same fixed value the rest of
  the system already uses for "the current user," applied to file
  ownership rather than a per-file guess at plausible ownership.
- **`atime_ms`/`mtime_ms`/`ctime_ms`:** real host wall-clock milliseconds
  (`kernel->host->wall_clock_millis()`, already implemented and used by the
  scheduler — not a new host capability, just a new consumer of an existing
  real one), not a fabricated or fixed value. Recorded at node creation
  (`node_create`); `mtime`/`ctime` update on `write()` and `truncate()`;
  `ctime` updates on `rename()` (a metadata change, matching real BSD
  semantics); `atime` updates on `read()`. `translate_stat` converts the
  stored milliseconds to `struct timespec` at the libc boundary.

## Backward compatibility: the struct_size guard

`CB_STAT_V1_METADATA_MIN_SIZE` (`include/cannedbsd/abi.h`) is defined via
`offsetof`/`sizeof` against the struct's own fields, not a hardcoded byte
count — this matters on a target (Mac68k) that compiles this same struct
with different alignment rules than x86-64/aarch64 Linux; a literal byte
number would silently be wrong there while a `sizeof`/`offsetof`-based
constant tracks whatever the target's own compiler actually produces.

`translate_stat` (`libc/cb_libc.c`) checks `raw_stat->struct_size >=
CB_STAT_V1_METADATA_MIN_SIZE` before reading the new fields; below that, it
falls back to the exact fixed sentinels this function always returned
before this ID (`st_dev = 1`, `st_nlink = 1`, `st_uid = st_gid = 0`, all
timestamps zero) rather than reading uninitialized memory past what an old
caller declared. `tests/libc_stat_oldtable_probe.c` (an ordinary-source
probe) plus `statoldtableprobe_main` (`tests/test_core.c`, following the
established `direntoldtableprobe_main` pattern of wrapping one `cb_api_v1`
function pointer rather than editing the real implementation) prove this
by wrapping the real `stat()` to truncate its own reported `struct_size`
after letting it populate fully — proving the guard is what suppresses the
new fields, not that the runtime lacks them. `fsstat01probe_main` proves
the real, non-fallback values separately: `/`'s bootstrap layout (`bin`,
`tmp`, `home`, `home/user`) gives known, checkable `nlink` counts (5, 2, 2,
3), a real non-zero device id shared consistently across `/` and `/bin`,
`uid`/`gid` both 0, non-zero timestamps, and `mtime` advancing after a
real `write()`.

## A real regression found and fixed along the way

`tests/libc_file_probe.c`'s `cp-stub-probe` mode had a pre-existing
assertion (predating this ID, from the struct-stat-growth work under
`MV-01`/`LIBC-CP-STUB-01`) that `stat("/")`'s timestamps are always
exactly zero — correct for the fixed-zero stub that existed before this ID,
now false. Confirmed by bisection (stashing this ID's changes reproduced a
clean pass on the same base; restoring them reproduced the failure) that
this was a genuine regression this ID's own timestamp implementation
caused, not a pre-existing issue. Updated the assertion to check
non-zero-ness instead of an exact stub value, matching the `CAT-01`
precedent: a design change should invalidate the tests that encoded the
old design, not survive alongside them. (A first attempt at the fix
compared `st_atime` to `st_atimespec.tv_sec` directly — Clang's
`-Wtautological-compare` correctly flagged this as always-false, since
`st_atime` is itself a macro for `st_atimespec.tv_sec`; simplified to a
plain non-zero check.)

## Woodpecker evidence

Per the coordinator's standing instruction, `ci`/`mac68k`/`mac-automation`
are reported separately from the Woodpecker API for this exact branch SHA,
not inferred from a local container run — see the completion report. The
two known pre-existing `mac68k` causes (missing `MAXBSIZE` include path on
`platform/mac68k/CMakeLists.txt`'s probes, and no CMake target for
`cat`/`cp`/`ls`/`mv`/`rm`) are unrelated to this ID and are tracked
separately (`work/MAC68K-INC-01`, `MAC68K-CMD-01`).
