# FORMAT-SIZE-01: size_t in the measured cat warning

- Status: candidate; exact CI and independent review reported in handoff.
- Base SHA: `61beeac0beef50a086622e6ffe734e10e15b5273` (coordinator-assigned
  combined candidate, exact #526 all three workflows passed; not guest-qualified).
- Branch: `work/FORMAT-SIZE-01`, new isolated sibling worktree.
- Hypothesis: consuming the pinned cat warning's actual `size_t` operand with
  `va_arg(arguments, size_t)` restores the complete allocation-failure warning.

## Source and scope

Unchanged pinned `upstream/netbsd/bin/cat/cat.c:304` calls
`warnx("malloc, using %zu buffer", bsize)` after its allocation fails. `bsize`
is declared `size_t` at line 65. It reports the failed request (4099 in the
fixture), before selecting its 1024-byte fallback at lines 307–308.

Only `format_output` gains `%zu`, through the existing printf/fprintf and
err/warn family. Existing bounded unsigned width handling applies. The separate
snprintf parser still rejects `%zu` with EINVAL: no actual pinned caller
requires it. Signed `%zd`, other z conversions, precision, and other unsupported
format forms remain unsupported. No parser refactor, public declaration,
shared ABI, imported source, runtime ownership, or host API changes.

## Red

Tests were changed and executed before the implementation. Linux runner used
`tribblix-woodpecker-agent:3.18.0`, image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
in an isolated source export with Docker network disabled and `/bin/sh` entrypoint.
The assigned base's exact CI is the baseline; no redundant full local rerun.

After `make LDLIBS=-lucontext build/test_core`:

- `./build/test_core --statics-cache-case 5` failed with
  `statics cache case 5 status 62`. The previously prefix-only comparison now
  requires exactly `cat: malloc, using 4099 buffer\n`, along with the existing
  successful command/session status, complete output, repeated request and
  allocation cleanup checks.
- `./build/test_core --err` failed at `formatprobe z A`: expected status 0 and
  `0|18446744073709551615|7|end|    42|123|   9`, actual status 29 and no output.

## Green

After the implementation, rebuilding and running `./build/test_core
--statics-cache` and `./build/test_core --err` passed. The ordinary-source probe
uses genuine size_t zero and maximum operands, checks printf/fprintf exact byte
counts and streams, preserved errno, narrow/wide/dynamic padding, subsequent
unsigned/string arguments, and unsupported z forms without variadic operands.
Existing signed, long, long-long, fixed-width, error, and stream tests remain.

The implementation reads size_t directly, never guesses its type from width.
The 32-bit expected maximum is 4294967295 and 64-bit maximum is
18446744073709551615; the executed local Linux checks use the latter.

Full gate is the exact pushed Woodpecker `make LDLIBS=-lucontext
SANITIZE_CC=clang ci`, plus mac68k and mac-automation workflows; results belong
in the handoff. `git diff --check` passes. No guest was operated: fresh exact
Mac artifact execution and native Solaris qualification remain coordinator
gates. The allocation-failure probe remains in Linux core tests.

## Shared Mac coverage follow-up

The coordinator extended this assignment to make the required fresh guest run
exercise the formatter changes. CMake now builds the existing ordinary format
probe and its owned module; both Mac main and the scoped Linux Mac fixture
register it with checked results. Two appended shared cases execute q A
(uint64/int64 zero and extrema plus independent l/ll conversions) and z P
(size_t zero and UINT32_MAX, widths, following arguments, and rejected z forms).
The z P value is explicitly bounded to UINT32_MAX so expected bytes are identical
on 32-bit Mac and 64-bit Linux; Linux z A retains native SIZE_MAX coverage.

All original 80 records remain, followed by the two new format cases. The
transcript derives 82 PASS records, 2092 bytes and a 2093-byte buffer including
NUL. The 21 host protocol tests pass, including exact rejection of the former
80-record result; prior 69/79 controls now explicitly omit the new cases too.
Build parity passes. Linux --err and --mac-acceptance results are recorded in
handoff. Mac compilation alone does not establish 32-bit execution: the fresh
coordinator guest gate remains required.
