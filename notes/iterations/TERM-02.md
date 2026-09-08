# TERM-02: console classification and honest attribute fallback

- Status: implementation ready for exact CI and coordinator-owned guest acceptance.
- Base: `d3884af7975e0e9e8d401ef94b75a1447a7c70d9`, freshly fetched main.
- Branch: `work/TERM-02`; worktree: `../bsdinacan-TERM-02`.
- Hypothesis: ordinary source can identify inherited/duplicated cannedBSD console
  descriptors while unavailable terminal attributes fail honestly, without
  consuming, editing, echoing, or translating legacy input.

## Red

Baseline `make LDLIBS=-lucontext clean test` passed in a disposable copy on biggie
using `tribblix-woodpecker-agent:3.18.0`.

Before implementation, `tests/libc_terminal_probe.c` included only ordinary
`unistd.h` and required `isatty(STDIN_FILENO) == 1`. Running
`cc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude -Ilibc/include -c tests/libc_terminal_probe.c -o /tmp/terminal.o`
failed at line 7 with `implicit declaration of function 'isatty'` and exit 1.
This demonstrated the absent private interface. The attribute, ownership and
short-table regressions were then added with the implementation; they are not
claimed as separate pre-implementation red evidence.

The first full gate also caught an actual test-fixture limit: adding another
probe to the full native registry failed `test program registration`. Capacity
remains 64 and every original probe/assertion remains. Shared acceptance now
selects `FIXTURE_MAC`, whose `register_mac_probes()` explicitly registers only
its six ordinary probes and checks each result. Future shared probes must be
registered there and in Mac main; no command-string selection is used.

## Validation

- Focused: `make LDLIBS=-lucontext build/test_core && build/test_core --terminal`.
  Passed. After the explicit fixture split, both `build/test_core --terminal`
  and `build/test_core --mac-acceptance` passed again.
- Full gate: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in a disposable Linux
  container, including ASan/UBSan, source/symbol boundaries, static analysis,
  all original runtime tests and Mac host protocol tests: passed before the
  final fixture split. The final commit must repeat the full gate in Woodpecker.
- Exact feature Woodpecker `ci`, `mac68k`, and `mac-automation`: required after
  push; exact commit/status URLs belong in the coordinator handoff.
- Guest acceptance: **pending**, serialized slot owned by the coordinator.
  The exact Mac artifact now runs unchanged ordinary `libcterminalprobe` in
  the shared suite (18 transcript records including the context check).
  No raw-mode or Mac keyboard editing claim is made by this probe.

## Change and review

Each console open file refers to one kernel-embedded `cb_terminal_state`; dup
and child inheritance retain that owner through their existing open-file
references. No extra allocation, queue, host callback, raw lease, or cleanup
path is introduced. Non-console files retain a null terminal owner.

`isatty` returns 1 for that emulated console, 0/`ENOTTY` for files/pipes and
0/`EBADF` for invalid descriptors. A redirected descriptor numbered 0 remains
a regular file. Success preserves task errno. `ENOTTY` (25) is the sole new
private error; existing numbers are unchanged.

`tcgetattr`/`tcsetattr` validate the descriptor first, then null attributes
(`EINVAL`). Every non-null console request returns -1/`ENOSYS`, including all
request actions and zero-filled or unrecognized proposed attributes. They
never dereference or alter the supplied profile. The private versioned
`cb_termios_v1` reserves four flag words and four control bytes; `termios.h`
advertises request names only, no supported modes, flags, or control indices.
Actual attribute negotiation/validation belongs to the later approved stage.

The three public callbacks append after `poll`. Startup still accepts its
previous mandatory prefix. Each wrapper checks its own complete field extent
before accessing the pointer, then checks NULL independently. `poll` now also
uses its own field end instead of the enlarged table size, preserving older
poll-capable API tables. Tests use actual shortened heap allocations (including
one byte short of each new callback), independently missing callbacks, and
present-neighbor operations to prove the guards do not mask each other.

Ordinary-source tests run through private headers and separately compiled
objects on Linux and Mac. Native tests assert probe/child statuses, shared
ownership, descriptor lifetime, errno across yields, and byte-for-byte legacy
input using CR, erase and EOF bytes, with no host calls caused by rejected
attribute requests. Existing full terminal/scheduler/poll tests remain intact.

Documentation: this note and `LIBC.md`. Shared backlog/status files are untouched.
Later canonical storage and real host integration remain separate tasks blocked
as described in the approved TERM-01 design; TERM-02 does not change that scope.
