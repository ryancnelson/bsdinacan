# ECHO-01: preparation only (prerequisites not yet integrated)

- Status: preparation, explicitly not implementation. `PROGNAME-01`
  (`work/PROGNAME-01`, two commits, not yet merged) and `STDOUT-01`
  (not yet started as of this note) are unmerged prerequisites this
  design (`notes/iterations/ECHO-01-design.md`) requires before the
  imported command can even compile, let alone pass a falsifiable red
  test or a real CI gate. This note records what was prepared now and
  what remains, without claiming any of the remaining work is done.
- Base SHA: `0423c31` (`origin/main`, freshly fetched).
- Branch: `work/ECHO-01`.
- Authorization: coordinator-authorized preparation while prerequisites
  integrate; explicitly no fake stubs and no success claims ahead of
  `PROGNAME-01`/`STDOUT-01` landing.

## What was actually done

1. Vendored `bin/echo/echo.c` unchanged at the pinned revision
   (`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`) to
   `upstream/netbsd/bin/echo/echo.c`. Hash verified directly against a
   fresh download: `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04`,
   matching both the roadmap's own measured hash and the file as
   fetched here -- not merely trusted from the roadmap document.
2. Added the `UPSTREAM.md` provenance entry, explicitly marked
   "preparation only, not yet compiled or linked" so the record cannot
   be misread later as claiming a working import.
3. **Deliberately not done, and why**: no Makefile rule, no module
   file, no `commands/echo_module.c`-equivalent, and no registration
   anywhere. The vendored source calls `setprogname`, `putchar`,
   `fflush`, and `ferror` -- none of which this base declares or
   implements as private veneers (confirmed by reading
   `include/cannedbsd/libc.h` and `libc/cb_libc.c` directly in this
   worktree, not just trusting the roadmap's own diagnostic). Adding a
   compile rule now would either reproduce that exact failure (useless
   as evidence -- it was already measured) or require inventing stub
   declarations for `setprogname`/`putchar`/`fflush`/`ferror` ahead of
   `PROGNAME-01`/`STDOUT-01`'s own real design, which the coordinator's
   authorization explicitly forbids ("no fake stubs or success claims").

## Prepared, not-yet-executable test matrix

These are the exact command-level cases this iteration's eventual
`tests/test_echo_behavior.sh` (or equivalent) must assert once the
command actually builds, derived directly from reading the pinned
source's control flow (not guessed): it does not call `getopt`,
recognizes only a literal first `-n` argument, never interprets `--`,
`-e`, or backslash sequences as special, and joins operands with a
single space.

| Command | Expected stdout | Expected stderr | Status |
| --- | --- | --- | --- |
| `netbsdecho` (no args) | `\n` | (none) | 0 |
| `netbsdecho ""` | `\n` | (none) | 0 |
| `netbsdecho " "` | ` \n` | (none) | 0 |
| `netbsdecho -n foo bar` | `foo bar` (no trailing newline) | (none) | 0 |
| `netbsdecho -- foo` | `-- foo\n` (`--` is an ordinary operand) | (none) | 0 |
| `netbsdecho -e foo` | `-e foo\n` (`-e` is an ordinary operand, not an escape flag) | (none) | 0 |
| `netbsdecho 'a\nb'` | `a\nb` + a real trailing newline (the two source characters backslash-`n` are printed literally, not interpreted) | (none) | 0 |

The remaining case the design and roadmap both call for --
"earlier output failure and a subsequent independent task's success"
-- cannot be written as a concrete command line yet: it requires
`STDOUT-01`'s task-owned output-error state and a way to force a real
write failure (mirroring this project's existing fault-injection
patterns, e.g. the pipe-capacity and allocation-failure probes used by
earlier iterations), neither of which exists on this base. Once
`STDOUT-01` lands, this case should: force a write failure on one
task's stdout (so `ferror(stdout)` is nonzero and the pinned source's
own `if (ferror(stdout) != 0) err(1, "write error");` fires, producing
exact stderr matching `err(3)`'s format and status `1`), then run a
second, independent task's `netbsdecho` to completion and confirm it
succeeds with completely normal output -- proving the error state is
genuinely per-task, not a shared/global flag that would incorrectly
contaminate the second task.

## Remaining steps (not started here)

1. `PROGNAME-01` and `STDOUT-01` merge to `main` (both coordinator/other
   workers' responsibility, tracked in their own branches and design
   doc; not touched by this preparation).
2. Once both are available on a fresh `main`, rebase this branch (or
   re-fetch into a fresh worktree, matching this session's established
   practice when a dependency's base moves mid-task) and add the actual
   compile rule, distinct object name, `commands/echo_module.c`-style
   registration under the name `netbsdecho` (per the design's item 3 --
   deliberately not replacing the existing shell builtin `echo`), and
   wire the prepared test matrix above into a real, executable
   `tests/test_echo_behavior.sh`, plus the write-failure/independent-task
   case once `STDOUT-01`'s fault-injection surface exists.
3. A genuine falsifiable red (the command fails to link without its
   own module/registration, exactly as the other imported commands in
   this project have done), full `make ci`, and a fresh Mac guest
   artifact run (coordinator-owned) before this item is considered
   done.

## Constraints preserved

- `CB_MAX_PROGRAMS` (64) is untouched; no registration was added at
  all in this preparation pass, so there is nothing to scope into a
  fixture yet. The eventual implementation should follow the existing
  `FIXTURE_DIRNAME`/`FIXTURE_DIRENT`/`FIXTURE_BASENAME` scoped-fixture
  pattern for its own native test coverage rather than growing
  `FIXTURE_FULL`.
- No guest/Basilisk II acceptance attempted; that remains
  coordinator-owned per this task's direction.
- No merge to `main` attempted or implied.
