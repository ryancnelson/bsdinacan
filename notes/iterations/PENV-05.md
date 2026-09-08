# PENV-05: `errx(3)` diagnostic

- Status: in progress
- Base SHA: `014c2a6` (local `work/team-integration-20260907`, per coordinator's
  explicit dependency-base override — `PENV-02`/`PENV-04`/`PENV-03` are
  integrated there but not yet on `origin/main`)
- Branch: `work/PENV-05`
- Hypothesis: the bounded formatter (`PENV-04`) plus `exit` (`PENV-02`) can
  provide printenv's exact fatal diagnostic without a general stdio
  implementation.

## Red

- Command: `make LDLIBS=-lucontext test` (Alpine 3.22 container matching the
  Woodpecker agent image)
- Expected failure: an ordinary `errx` probe fails to link, because `err.h`
  and `errx` are declared but not defined.
- Observed failure: linking `build/test_core` failed with `undefined
  reference to 'cb_libc_errx'` from `build/errxprobe_command.o`
  (`tests/libc_errx_probe.c:40`) — the same link-time red pattern as
  `PENV-02`/`PENV-03`.

## Green

- Focused command: `make LDLIBS=-lucontext test`
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- Linux Woodpecker: pending push
- mac68k Woodpecker: not applicable (no `platform/mac68k` change)
- Guest acceptance: coordinator-owned; not performed in this session.

## Change and review

- Implementation: one new ABI operation, `getprogname`, returning
  `active_kernel->current->argv[0]` — reusing the task's own already-tracked
  `argv` (already correctly per-task and already correctly replaced on a
  successful `exec`) rather than introducing any new state to manage.
  Appended after `getopt_state_location` (the current end of `cb_api_v1`),
  preserving append order; no existing field reordered, no struct-size
  backward-compat check touched. `cb_libc_errx` (`libc/cb_libc.c`) writes
  `"<getprogname()>: "` then the caller's formatted message (via the
  already-`static` `format_output`/`write_all` helpers `PENV-04` added —
  reused directly, not reimplemented, since `cb_libc_errx` lives in the same
  translation unit) then `"\n"`, all to descriptor 2 directly, then calls
  `cb_libc_exit(eval)` (`PENV-02`), which never returns. `err.h`
  (`libc/include/err.h`, new) declares `void cb_libc_errx(int eval, const
  char *fmt, ...) __dead;` and maps `errx` to it — `__dead` lives only on
  this ordinary-facing declaration, not in `include/cannedbsd/libc.h`,
  matching `PENV-02`'s `exit` precedent exactly (that header is compiled
  without `-Ilibc/include` in some contexts, so it cannot reach
  `sys/cdefs.h`).
- ABI, ownership, and cleanup review: append-only, one op, no reordering.
  Confirmed `abiprobe`'s existing `api->struct_size != sizeof(*api)` and
  per-field non-`NULL` checks are unaffected — only appended to (see updated
  `abiprobe_main`, requiring `api->getprogname != NULL`). No new allocation,
  no new task-struct field: `getprogname` is a pure read of already-owned,
  already-correctly-lifecycled memory.
- Documentation: `LIBC.md` gains a new `err.h` bullet.
- Remaining risk or follow-up: `errx`'s message formatting inherits
  whatever `PENV-04`'s bounded formatter supports (literal text and `%s`
  only) — no broadening attempted here. Guest acceptance under AGENTS.md
  step 7 is outstanding, coordinator-owned.
