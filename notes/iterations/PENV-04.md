# PENV-04: bounded unbuffered formatted output

- Status: in progress
- Base SHA: 789ceb7
- Branch: `work/PENV-04`
- Hypothesis: literals, `%%`, and `%s` cover every format used by pinned
  `printenv` while retaining task-descriptor errors without buffered state.

## Red

- Command: `make build/libc_stdio_source.o`
- Expected failure: ordinary source cannot declare or call `printf`, `fprintf`,
  `stdout`, or `stderr` through the cannedBSD libc veneer.
- Observed failure: compilation reported implicit declarations for `printf` and
  `fprintf` and undeclared identifiers `stdout` and `stderr`.

## Green

- Focused command: `make clean test LDLIBS=-lucontext`
- Full command: `make ci`
- Linux Woodpecker: pending
- mac68k Woodpecker: pending
- Guest acceptance, when required: pending

## Change and review

- Implementation: Added opaque private stdout/stderr handles and bounded,
  unbuffered printf/fprintf support for literals, `%%`, and `%s` only.
- ABI, ownership, and cleanup review: No ABI operation or buffered stream state
  was added. Every output byte uses the current task's descriptor API; partial
  writes are retried and descriptor errors remain task-local.
- Documentation: Updated `LIBC.md` with the exact supported subset and failure
  behavior.
- Remaining risk or follow-up: Width, precision, numeric conversions, files,
  and buffering remain intentionally absent until a source consumer requires
  them.
