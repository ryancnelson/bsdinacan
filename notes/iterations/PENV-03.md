# PENV-03: empty-option `getopt`

- Status: in progress
- Base SHA: 78a1e5b
- Branch: `work/PENV-03`
- Hypothesis: task-local getopt state can support the exact empty optstring
  used by pinned `printenv` without claiming the full extension surface.

## Red

- Command: `make LDLIBS=-lucontext test` (Alpine 3.22 container matching the
  Woodpecker agent image; this host is macOS, not a supported Linux host)
- Expected failure: an ordinary argv probe covering no options, `--`, and an
  unknown option fails to link.
- Observed failure: linking `build/test_core` failed with `undefined
  reference to 'cb_libc_getopt'` and `'cb_libc_getopt_state_location'` from
  `build/getoptprobe_command.o` (`tests/libc_getopt_probe.c`). The probe
  compiled cleanly — `unistd.h` already declared `optind`/`optarg`/`opterr`/
  `optopt`/`getopt` mapped to the private veneer — only the definitions were
  missing, a genuine link-time red like PENV-02's.

## Green

- Focused command: `make LDLIBS=-lucontext test`
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- Linux Woodpecker: pending push
- mac68k Woodpecker: not applicable (no `platform/mac68k` change)
- Guest acceptance: required by the new AGENTS.md step 7 (this is a libc
  change) — not performed in this session; the coordinator owns the
  serialized Basilisk II slot.

## Change and review

- Implementation: one new ABI operation, `getopt_state_location`, returning
  the address of a per-task `struct cb_getopt_state_v1` (`optind`, `opterr`,
  `optopt`, `optarg`, plus a private `place` scan cursor) — appended after
  `environ_location`, same pattern as PENV-01. The cursor is bundled into the
  same task-owned struct rather than a `cb_libc.c` file-static, because a
  static cursor would reintroduce exactly the shared-global bug this whole
  `PENV` series exists to eliminate. Defaults (`optind=1, opterr=1, optopt=0,
  optarg=NULL, place=""`) are set in `task_create` and reset identically in
  `task_finish_exec`, matching how `errno`/`environ` are already handled.
  `cb_libc_getopt()` (`libc/cb_libc.c`) is the classic NetBSD/BSD
  single-dash `getopt(3)` algorithm (no GNU long options, no `::` optional
  arguments — those are a different, unclaimed extension surface), operating
  entirely through the state struct rather than any local statics. Diagnostic
  printing on `opterr` is intentionally not implemented: there is no
  formatter yet (that is `PENV-04`), and printing nothing when `opterr` is
  merely *tracked* is not a missing behavior the accept criteria asks for.
- ABI, ownership, and cleanup review: append-only ABI change, no reordering;
  `struct cb_getopt_state_v1` is embedded directly in `struct cb_task` (not
  separately allocated) since nothing in the accept criteria requires an
  independent allocation, unlike `errno`'s cell. `getoptwaitprobe` proves
  isolation the same way PENV-01's `environprobe` did: role A repeats a
  "no options" `getopt()` check before and after being forced to block
  (writing past pipe capacity) while role B — a separate task — runs two of
  its own `getopt()` calls (an unknown option, then `--`) in between; role
  A's second check must still see `optind == 1`, proving role B's calls
  never touched role A's state. `execprobe`/`pidcheck` prove reset-on-exec:
  `execprobe` mutates `optind`/`opterr`/`optopt`/`optarg` away from their
  defaults immediately before `exec`, and `pidcheck` (the post-exec image)
  asserts all four are back to their fresh-task defaults.
- Documentation: `LIBC.md` gains `unistd.h`'s `getopt` surface.

## Review correction (post-push, same branch)

A design/coverage review of the first push (`3c7328f`) found two real
issues, both fixed in a follow-up commit on this same branch:

1. **Scope overclaim.** The first `LIBC.md` entry said `getopt` supports
   "any optstring," but only the empty-optstring/`--`/unknown-option surface
   was ever tested — the `:`-required-argument branch had zero coverage.
   Per AGENTS.md's "no speculative surface" rule, an untested branch
   shouldn't ship at all rather than ship-but-be-honest-about-it: the
   `:`-required-argument branch is now removed from `cb_libc_getopt`
   entirely (`libc/cb_libc.c`), and `LIBC.md` now says exactly what's true —
   flag-only optstrings, explicitly not the `:` convention. The core
   flag-matching algorithm (`strchr(optstring, ch)`) stays general (it has
   to be, to correctly reject an unknown option under an empty optstring at
   all), so this is a narrowing of the untested edge, not a rewrite.
2. **`opterr` was inert.** It was tracked (gettable/settable, isolated,
   reset-on-exec) but had zero effect on behavior — nothing ever branched on
   it. `cb_libc_getopt` now writes a real diagnostic to `stderr`
   (`"<argv[0]>: illegal option -- <c>\n"`, assembled with the same
   `write()`-per-fragment idiom `commands/wc.c`'s `report_error` already
   uses, since there is still no formatter) when an unknown option is hit
   and `opterr != 0`, and writes nothing when `opterr == 0`.
   `getopterrprobe`/`getoptwaitprobe` (`tests/test_core.c`) prove both
   sides: `getoptwaitprobe`'s existing unknown-option case runs with the
   default `opterr == 1` and now asserts the exact diagnostic text landed on
   the captured stderr stream; the new `getopterrprobe` spawns a task with
   `opterr = 0` and asserts the captured stderr stream gained zero bytes.

Additionally, per the same review, added `getoptclusterprobe`
(`tests/test_core.c` + new roles C/D in `tests/libc_getopt_probe.c`):
role C's argv contains one clustered token, `"-xy"` — two unknown options
packed into a single argv element. Its first `getopt()` call must consume
only `'x'` and leave the private scan cursor mid-element; it is then forced
to block (the same past-pipe-capacity mechanism as role A/B), during which
role D — a separate task — runs two of its *own* unrelated `getopt()` calls
to completion. Role C's second call, after resuming, must still resume
exactly at `'y'`. This was already correct by construction (the scan cursor
lives in the same per-task struct as `optind`/etc., not a `cb_libc.c`
file-static), but had no dedicated test proving the specific
multi-character-cluster-split-across-a-real-task-switch shape before this
correction.

- Remaining risk or follow-up: `cb_libc_getopt` deliberately does not
  implement the `:` required-argument convention (see correction #1 above)
  — a future consumer that needs it is a separate, independently
  red/green-tested extension. **Guest acceptance under the new AGENTS.md
  step 7 is outstanding** — this change touches libc, so it requires the
  exact `mac68k` Woodpecker artifact tested in the shared Basilisk II guest
  before integration; that slot is coordinator-assigned and was not run in
  this session.
