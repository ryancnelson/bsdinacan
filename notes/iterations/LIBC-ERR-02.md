# LIBC-ERR-02: `warnx`

- **Status:** implemented, `make ci` pending final confirmation.
- **Depends on:** FILEUTIL-01 (Done), ERR-01 (Done), ERR-02 (Done).
- **Origin:** found while re-measuring `RM-01`'s `rm.c` compile against
  the merged `STAT-02`/`FTS-CORE-01` tree (`notes/iterations/RM-01.md`).
  `rm_tree()`'s `FTS_DNR` case (`rm.c:191`) calls `warnx` unconditionally
  on ordinary `rm -r` against an unreadable directory -- not a deferred
  path, so it genuinely blocks `-r`. `ERR-02`'s own note explicitly
  scoped `warnx` out at the time ("Do not expand the format language or
  implement `warnx`"), leaving it as its own unclaimed item exactly as
  `BACKLOG.md` records it.

## Implementation

`cb_libc_warnx` in `libc/cb_libc.c`, placed next to `cb_libc_err`: the
same shape as `cb_libc_errx` minus the `eval` parameter and the
`cb_libc_exit` call at the end -- i.e. `warn`'s sibling with the errno/
`strerror` suffix removed, not `errx`'s sibling with the exit removed,
since `warnx` (like `warn`) must return to the caller. Follows `warn`'s
own established discipline exactly: snapshots `errno` via
`bound_api->get_errno()` before writing anything and restores it via
`bound_api->set_errno()` at the end, so a failed write to a closed/bad
stderr cannot leak into the caller's errno. `err.h` declares it and maps
`#define warnx cb_libc_warnx`, alongside the existing `warn`/`err`/`errx`
macros.

Given no separator between `progname: ` and the message (unlike `warn`,
which needs one before the appended `strerror` text), a `NULL` format and
an empty-string format now produce the *same* output, `"progname: \n"`
-- there is no second component for a `NULL` format to skip and an empty
one to still separate, unlike `warn`'s asymmetric `"warnprobe: no such
file..."` vs `"warnprobe: : no such file..."` distinction.

## Testing

`tests/libc_warnx_probe.c` + `tests/libc_warnx_probe_module.c`, byte-for-
byte structural mirror of `libc_warn_probe.c`/`_module.c`: ordinary
format, `NULL` format, empty format, and closed-stderr errno-preservation,
registered into the existing `FIXTURE_ERR` (shared with `warnprobe`, no
new fixture needed -- well under the `CB_MAX_PROGRAMS` ceiling).

## Not done

`platform/mac68k/main.c`/`acceptance_cases.def` Mac68k registration was
not added. `ERR-02`'s own precedent registered `warnprobe` there, but
Mac68k guest acceptance has been explicitly PENDING for every ID this
session per standing direction, and nothing in `make ci`'s Linux-side
gate (including the mac68k cross-compile step) required it. Left for
whoever next touches Mac68k acceptance for this batch, rather than added
speculatively without a guest run to actually exercise it.
