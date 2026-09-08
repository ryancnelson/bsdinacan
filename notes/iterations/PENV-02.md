# PENV-02: `exit(3)` and `__dead`

- Status: in progress
- Base SHA: 789ceb7
- Branch: `work/PENV-02`
- Hypothesis: the existing task-exit operation (`cb_api_v1.exit`, already used
  internally by the native executor after any program's `start` returns) can
  provide a non-returning ordinary C `exit(3)` while preserving the same task
  cleanup, without adding any new process API.

## Red

- Command: `make LDLIBS=-lucontext test` (Alpine 3.22 container matching the
  Woodpecker agent image; this host is macOS, not a supported Linux host)
- Expected failure: an ordinary-source probe calling `exit(7)` fails to link.
- Observed failure: linking `build/test_core` failed with
  `undefined reference to 'cb_libc_exit'` from `build/exitprobe_command.o`
  (`tests/libc_exit_probe.c:26`, the compiled-separately ordinary-main probe).
  The probe compiled cleanly — `stdlib.h` already declares
  `void cb_libc_exit(int status) __dead;` and maps `exit` to it — only the
  definition and ABI wiring were missing, giving a genuine link-time red
  rather than a compile-time one, matching the backlog's literal wording.

## Green

- Focused command: `make LDLIBS=-lucontext test`
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- Linux Woodpecker: pending push
- mac68k Woodpecker: not applicable (no `platform/mac68k` change)
- Guest acceptance, when required: not applicable

## Change and review

- Implementation: `cb_libc_exit(int status)` (`libc/cb_libc.c`) is a thin
  wrapper over the already-existing `bound_api->exit(status)` — the same ABI
  operation the native executor already calls automatically whenever a
  program's `start` function returns, and which already reclaims task
  allocations (`task_release_allocations`) and descriptors (`fd_close_all`)
  before marking the task a zombie and switching away
  (`cb_executor_request_termination`, which never returns to its caller).
  No new process API was added; `exit(3)` is purely a libc-level name for an
  ABI capability that already existed. `__dead`
  (`__attribute__((__noreturn__))`, guarded for GCC/Clang, in
  `libc/include/sys/cdefs.h`) is attached only to the ordinary-facing
  declaration in `libc/include/stdlib.h` — not to the private
  `include/cannedbsd/libc.h` layer, which stays attribute-free like every
  other `cb_libc_*` prototype there.
- ABI, ownership, and cleanup review: no ABI change at all — `cb_api_v1.exit`
  already existed and is unchanged. `api_is_usable()` in `cb_libc.c` now also
  requires `api->exit != NULL`, matching the existing style for other
  operations the veneer depends on. Heap and descriptor reclamation are
  proved by the new `exitwaitprobe`/`libcexitprobe` pair in `tests/test_core.c`:
  the child writes `"A"`, calls `exit(7)`, and (unreachably) would write `"B"`;
  the parent observes only `"A"` on the pipe (no fallthrough), observes
  `cb_test_task_allocation_count(child) == 0` while the child is still a
  zombie and not yet reaped (heap reclaimed synchronously during `exit`, not
  deferred to `waitpid`), observes the pipe's write end close on its own even
  though the child never called `close()` (descriptor reclaimed), and
  observes `waitpid` return status `7`.
- Documentation: `LIBC.md` gains `stdlib.h`'s `exit`/`__dead`.
- Remaining risk or follow-up: scope was deliberately kept to exactly
  `exit(3)` and `__dead` — no `atexit`, no `_Exit`, no `abort`, no broadening
  of the program-start ABI. `PENV-03`/`PENV-04` remain independent and
  untouched by this change.
