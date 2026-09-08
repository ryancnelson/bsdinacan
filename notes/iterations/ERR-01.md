# ERR-01: errno-bearing err(3)

- Base: `ca0cd19` (freshly fetched `origin/main`).
- Branch: `work/ERR-01`; worktree: sibling `bsdinacan-ERR-01`.
- Assigned source consumer: NetBSD `usr.bin/dirname/dirname.c`, revision
  `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, calls `err(1, "%s", *argv)`
  after `dirname` fails. No upstream file is imported or changed here.
- Hypothesis: existing errno, strerror, formatter, and exit operations can
  preserve the diagnostic's original error across partial writes and task
  switches without adding a runtime ABI operation.

## Red

Baseline `make LDLIBS=-lucontext clean test` passed in the existing
`tribblix-woodpecker-agent:3.18.0` image on biggie. Before adding the
implementation or declaration, the new ordinary-source probe failed:

```
cc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude -Ilibc/include \
   -Dmain=cb_err_probe_main -c tests/libc_err_probe.c -o /tmp/err-probe.o
tests/libc_err_probe.c:10:9: error: implicit declaration of function 'err'; did you mean 'errx'?
```

## Implementation and tests

`err` snapshots task errno before any writes, formats program name and the
optional message, appends the saved error's text and newline to fd 2, and exits
with the requested status. Formatting retains the existing literals, `%%`,
and `%s` boundary. Null versus empty format and diagnostic-write failure are
explicitly tested. This adds no buffering, host imports, or runtime ABI fields.

The ordinary probe is compiled once under private headers and linked into both
Linux tests and the Mac acceptance application. Its direct guest case checks
the exact ENOENT diagnostic and exit 7, expanding the shared suite to sixteen
PASS records. It is an acceptance-only program, not a new user command.

The Linux interleaving test runs two actual internal tasks with distinct
argv[0], errno, and exit status. A shared test API delegates real stderr I/O
in at most two-byte writes, overwrites errno with EIO, and yields during each
write. It asserts multiple writer switches and each task's exact diagnostic
bytes, plus child statuses 7/8 and no stdout. This proves errno is captured
before writes, messages survive real resumption, and short writes do not lose
bytes; it does not promise atomic stderr messages across tasks.

## Validation

- Focused `make LDLIBS=-lucontext build/test_core && build/test_core --err`
  passed in the Linux agent image.
- `build/test_core --mac-acceptance` passed the shared sixteen-record suite
  on Linux. This is not guest execution evidence.
- Host protocol tests: eighteen passed after updating the expected transcript.
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed in the Linux
  agent image, including sanitizer, build-mode, source-boundary, and analyzer checks.
- Exact feature Woodpecker checks: pending push.
- Exact-artifact System 7 acceptance: pending; coordinator owns the guest.

Changed documentation: this note and the `LIBC.md` interface inventory.
`dirname` import still needs its separate libgen and C-locale contracts.
