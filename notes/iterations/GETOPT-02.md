# GETOPT-02: required option arguments

- Status: implementation complete; exact CI and guest acceptance pending
- Base: `4ee800e` (fresh main)
- Branch: `work/GETOPT-02`
- Scope: the assigned required-argument dependency for unchanged head; no head
  import, diagnostic API expansion, optional arguments or option permutation.

## Hypothesis and actual red

The existing task-owned getopt parser can consume a single-colon required
argument without losing the independent cursor, option value and index across
cooperative task switches. The first ordinary-source regression parsed `-n10`
with `n:` and required return `n`, optind 2, optopt `n`, and optarg pointing
inside the original token at `10`.

Before editing the runtime, a disposable Linux x86_64 container using the CI
agent image ran:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --getopt-arg
```

Observed red: `FAIL: required getopt attached argument status 11`, exit 1.
The preserved pre-fix source archive has SHA256
`c94b6699754253028e44c4a931d7d0d36a2dd6e0b8793c2f73fd684f71d6f162`.
This was a behavioral failure, not a missing interface or compiler failure.

## Contract and implementation

The owned parser continues to stop at the first operand, single `-`, or literal
`--`. Required arguments may occupy the remainder of a cluster or the next argv
entry, including an empty string, `-q`, or `--`. A flag clears optarg; so do
unknown options, missing arguments and end-of-options. optopt keeps the option
just examined, including on failure, and optind advances past the option and
any separate argument. A leading colon suppresses diagnostics and changes only
missing-argument return from `?` to `:`; unknown options still return `?`.
opterr zero independently suppresses diagnostics. Colon is optstring syntax,
not an option character.

The required-argument and leading-colon rules agree with the documented subset
of [NetBSD getopt(3)](https://man.netbsd.org/getopt.3). The existing illegal-option
message remains unchanged. Missing values write the exact message
`argprobe: option requires an argument -- n` followed by newline for a caller
named `argprobe`. Diagnostics continue using the existing raw stderr write path.
No GNU permutation, long options, double-colon optional argument behavior,
optreset API, new allocation or ABI field is added. Invalid optstring extensions
are outside this task's supported contract.

## Executed test boundaries

`tests/libc_getopt_arg_probe.c` is ordinary source using only private standard
headers. Table cases assert the return, optind, optopt and exact borrowed optarg
pointer after every call. Each case starts in a fresh kernel/task rather than
resetting hidden parser state. Coverage includes attached/separate values,
clustered flags plus values, dash-leading/empty values, attached trailing flag
characters consumed as data, missing values alone/in clusters/after a previous
value, both suppression forms, unknown options after a value and in clusters,
colon syntax, operands, `-`, `--`, exhausted argv and no permutation.
Host capture checks exact separate stdout and stderr bytes, including silence.

The same shared module runs a real lifecycle sequence on native and Mac:

1. The owner pauses partway through `-qn10`; a spawned peer parses unrelated
   `-c -different` before the owner resumes and consumes `n` with `10`.
2. Another peer parses while the owner's optarg is live; the owner checks its
   index, option and pointer remain unchanged. A failed exec preserves them.
3. Successful exec from a real required argument resets optarg, and a separate
   exec from a mid-cluster cursor resets that cursor. Both new entries verify
   optind 1, opterr 1, optopt 0 and optarg NULL before parsing new arguments.

Registration, spawn, wait and exact child status are asserted. The module is
shared without host-only injection or manual state repair. Source/symbol checks
require prefixed getopt, task-state accessor and strcmp imports and reject host
getopt globals. All existing flag/getopt/printenv/dirname/basename tests remain.

## Validation and handoff

- Focused command above passed after implementation.
- `python3 -B tests/test_mac_guest.py`: all 18 tests passed.
- Full Linux command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed,
  including ASan/UBSan, build variants, repeated normal tests and analysis.
- Exact pushed-commit Woodpecker ci/mac68k/mac-automation: pending.
- Guest: coordinator-owned serialized acceptance pending; no guest pass claimed.
- Three added shared records (`getoptargs attached`, `getoptargs missing`, and
  `getoptargs lifecycle`) preserve the original 43 for **46 expected records**.
  Native fixtures stay scoped and the 64-program limit is unchanged.
- Only the existing getopt implementation, tests/build wiring and this note
  change. Shared capability/backlog rollups remain coordinator-owned.

## Coordinator acceptance, 2026-09-08

Integration `b2dc0ce9fef946d2ae6a7c6e800888c785253101` is merged to main
after independent review and exact Woodpecker #254 ci/mac68k/mac-automation
success. Archive SHA256:
`64e508437e92fc23e55389aa94a3b130eec5cd7032b03433f6b7d87c0bf08862`.
Fresh guest `run-j5c9_75j` produced all 50 expected PASS records and ALL PASS.
The coordinator inspected the decoded screenshot and receipt, confirming app
closure, closed guest disks and slot release after normal shutdown. The cold
automated cycle took 24.58 seconds; no manual correction or resume was needed.
All 43 prior records remain, with four real netbsdecho cases and three required
getopt argument/diagnostic/task-lifecycle cases appended.
