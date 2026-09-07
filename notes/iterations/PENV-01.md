# PENV-01: task-local libc process state and `environ`

- Status: done
- Base SHA: 873807d
- Branch: `work/PENV-01`
- Hypothesis: a task-local libc location/accessor can expose the current
  task's environment without leaking it during cooperative interleaving.

## Red

- Command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` (Alpine 3.22 container
  matching the Woodpecker agent image; this host is macOS, not a supported
  Linux host, so a container is the documented substitute per AGENTS.md)
- Expected failure: an ordinary-source probe referencing `environ` and an
  ABI-level test referencing `api->environ_location()` both fail to compile,
  because no such accessor exists yet.
- Observed failure: `build/test_core` failed to compile with
  `'const struct cb_api_v1' has no member named 'environ_location'` at every
  new call site (`pidcheck_main`, `environpeer_main`, `environprobe_main`).

## Green

- Focused command: `make LDLIBS=-lucontext test` — passed, including the new
  `environprobe`/`environpeer` cooperative-interleaving case and the
  `libc_environ_source.c` boundary check.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` — passed
  end to end (publication hygiene, optimized build+suite, Clang ASan/UBSan
  build+suite, build-mode isolation, second optimized build+suite, GCC
  `-fanalyzer` and the architecture boundary scan) inside an Alpine 3.22
  container built to match `CI.md`'s documented Woodpecker agent image
  (`bash build-base clang20 compiler-rt git libucontext-dev python3
  ripgrep`), run against this worktree with the main checkout's `.git`
  mounted at its real host path so the `git worktree` metadata resolves.
- Linux Woodpecker: **success** on commit `cea0361d61a616283a10668df5cbc999dc2765fe`
  (`ci/woodpecker/push/ci`, pipeline #27/1).
- mac68k Woodpecker: **success** on the same commit (`ci/woodpecker/push/mac68k`,
  pipeline #27/2) — unaffected, since this change touches no
  `platform/mac68k` file; the cross-build and Retro68 package still succeed.
- Guest acceptance, when required: not applicable.

Two bugs surfaced and were fixed while getting to green, both in the new test
rather than the implementation:
- The probe originally passed its own (stale, pre-mutation) `envp` argument to
  `api->spawn()` for the peer, instead of `NULL`. That made the peer inherit
  the probe's *original* startup environment rather than its current
  (post-`setenv`) one — precisely the kind of bug this feature exists to
  prevent, just relocated into the test. Fixed by spawning with `NULL` so the
  peer inherits the parent's live environment at spawn time.
- The final post-`waitpid` assertion assumed the mutated `TOKEN` entry sits at
  index 0 of the environment vector. It doesn't: this task inherits `HOME` and
  `PATH` from the booting shell first, so `TOKEN` lands later. Fixed by
  asserting through `api->getenv("TOKEN")` instead of an index.
- Both were caught by temporarily wrapping every `return <code>;` in the new
  functions with an `fprintf(stderr, ...)` trace, rerunning, and decoding the
  observed exit status back to the source line (e.g. 276 % 256 == 20 — the
  shell masks a child's exit status to 8 bits, which is why the first
  observed failure read as status 20 rather than the traced 276). Removed
  before the green run above.

## Change and review

- Implementation: append `char ***(*environ_location)(void)` to
  `cb_api_v1` (`include/cannedbsd/abi.h`); implement it in `src/core.c` as
  `&active_kernel->current->environment` (same pattern as
  `api_errno_location`); expose it through the libc veneer as
  `cb_libc_environ_location()` (`libc/cb_libc.c`, `include/cannedbsd/libc.h`)
  and the ordinary-source macro `#define environ (*cb_libc_environ_location())`
  in `libc/include/unistd.h`.
- ABI, ownership, and cleanup review: the new field is appended after
  `errno_location`, so existing struct layout and `struct_size` checks are
  unaffected; no reordering. `environ_location` returns the address of the
  task's own `environment` field, so it always reflects the latest pointer
  after `setenv`/`unsetenv` resize it or after a successful `exec` replaces
  it — no separate cache to go stale, and nothing to free (ownership stays
  with the existing environment-vector lifecycle).
- Documentation: `LIBC.md` gains `unistd.h`'s `environ`; `CURRENT-STATE.md`
  gets an iteration entry after merge (integrator's rollup, per AGENTS.md).
- Remaining risk or follow-up: `CB_LIBC_PROGRAM`'s adapter still discards the
  `envp` parameter passed to an ordinary program's start function
  (`(void)envp;`) rather than forwarding it to a 3-argument `main`. This is
  intentional here — `environ` is sourced live from the per-task kernel state
  via `environ_location()`, not from the `envp` the runtime happens to pass to
  program start, so nothing is lost. Flagging this explicitly because the
  backlog's acceptance text says "startup no longer discards envp," which
  could be read as requiring 3-argument `main` forwarding; I judged the
  live-accessor design to satisfy the underlying concern (no environment loss,
  no unscoped host global) more robustly than snapshotting envp at startup
  would. Worth a second pair of eyes at merge.
