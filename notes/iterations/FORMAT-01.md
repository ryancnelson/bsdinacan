# FORMAT-01: bounded signed decimal and width-qualified string formatting

- **Status:** Done. Rebased `c0d36f7`'s existing `%Nd` implementation onto
  current `origin/main`, extended to cover width-qualified `%s`, and
  verified against real consumers. Full `make ci` green.
- **Base SHA:** `7b1a152` originally; rebased onto `origin/main` at `89ef526`
  (post-`CAT-01`) via `work/FORMAT-01`.
- **Branch:** `work/FORMAT-01`.
- **Measured consumers (named per the coordinator's scope note):**
  - `cat -n`/`-b` (`CAT-01`): `fprintf(stdout, "%6d\t", ++line)` and
    `fprintf(stdout, "%6s\t", "")` for the blank-line-continuation case
    under `-b`. This is what originally forced the `%Ns` extension --
    `FORMAT-01-design.md` had explicitly excluded width-qualified `%s` on
    the grounds `uniq` never needed it, and `cat -b` demonstrated that
    scope insufficient.
  - `ls -l` and `wc` (queued as `LS-02`/`WC-02`, not yet imported): named
    by the coordinator as additional consumers needing width-qualified
    numeric/string columns for aligned multi-field output (link counts,
    sizes, multi-count lines). Verified the mechanism generalizes to
    multiple independently-widthed conversions concatenated in one
    output line (see "Multi-field verification" below) since neither is
    imported yet to measure their exact format strings directly.

## Red (original, 2026-09-13)

Validation used an isolated Ubuntu 24.04 ARM64 Linux container with GCC and
Clang. Local Linux result, not Woodpecker or native guest evidence.

- Command: `make build/test_core && build/test_core --format`.
- Ordinary source calls `printf("%4d", -42)` with errno set to `ENOENT`.
- Expected: status 0, exact output ` -42`, return count 4, preserved errno.
- Observed: build succeeded; test exited 1, reporting
  `actual status/output: 17 <>` against `expected status/output: 0 < -42>`.
- The production formatter was unchanged at that checkpoint -- a
  pre-implementation behavioral failure, not a missing-declaration one.

## Re-measurement (2026-09-17, before extending)

`work/FORMAT-01` at `c0d36f7` was 67 commits behind `origin/main` (predates
`RM-01`, `MV-01`, `CAT-01`, and their whole prerequisite chain). Rather
than trust the 2026-09-13 note's "implementation pending" status, rebased
onto current `main` and re-ran `build/test_core --format` before touching
anything: **it already passed.** `c0d36f7`'s `%Nd` implementation
(bounded width 1-32, `INT_MIN`-safe unsigned-magnitude conversion, left-
space padding, no flags/precision/length-modifiers/other conversions)
merged cleanly against current `libc/cb_libc.c` and was already correct
against real consumers -- the stale note undersold work that had, in
fact, already reached green; it was simply never carried through review
or merge.

## Extension: width-qualified `%s`

`format_output` (`libc/cb_libc.c`) previously special-cased a bare `%s`
before any width parsing, so `%Ns` fell into the `%d`-only width-then-
conversion dispatch and was rejected with `EINVAL` regardless of the
digit(s) preceding `s`. Restructured so width parsing is shared: after an
optional bounded 1-32 digit prefix, the conversion character determines
`%d` (existing decimal logic, unchanged) or `%s` (new: right-justify like
`%d` -- pad with spaces if the string is shorter than the field, never
truncate if it is already wider, matching `%d`'s own non-truncating
policy for an over-wide value). Any flag, precision, length modifier, or
other conversion on `%s` remains an immediate `CB_EINVAL`, exactly as for
`%d` -- `%-4s`, `%.1s`, `%33s` (width overflow) all still rejected.

`tests/libc_format_probe.c`'s `bad[]` table lost `%4s` (now valid) and
gained `%-4s`/`%.1s`/`%33s`. Its `cases[]` table gained `%4d:%6s` (proves
padding) and `%4d:%1s` (proves non-truncation). A dedicated `n` mode
reproduces `cat.c`'s exact call shape, `fprintf(stdout, "%6s\t", "")`,
end to end.

## Multi-field verification (coordinator's scope note)

Before naming `ls -l`/`wc` as consumers, verified the mechanism is not a
single-field special case: `libc_format_probe.c`'s new `m` mode calls
`fprintf(stdout, "%3d%6d%8s%1d", 1, 22, "tail", 333)` in one line --
four conversions, four independent widths, mixing `%d` and `%s`. Expected
and measured output: `"  1"` + `"    22"` + `"    tail"` + `"333"` =
`"  1    22    tail333"` (20 bytes, matching the returned count). Each
conversion's width is parsed and applied independently within the same
`format_output` call; there is no shared or leaking width state between
fields. This generalizes to right-aligned numeric columns of differing
widths in one output line, which is what `ls -l`'s column alignment and
`wc`'s multi-count output will need once imported.

No scope expansion beyond width qualifiers: no precision, no flags
(`-`/`+`/`0`/` `/`#`), no floating point, no length modifiers -- all
still immediate `CB_EINVAL` on both `%d` and `%s`, verified by the
expanded `bad[]` table.

## Green

`make LDLIBS=-lucontext test` and `make LDLIBS=-lucontext SANITIZE_CC=clang
ci`: full pass (normal, sanitizer, isolation builds), including the
restored `cat -n`/`-b` cases in `tests/test_cat_behavior.sh` (previously
excluded per `CAT-01`'s Finding 3) now producing real, correct numbered
output end to end -- measured via `cat -n`/`cat -b` against real files,
not just the formatter probe in isolation.

## Change and review

The pre-existing ordinary unsupported-`%d`/stdio-state `%d` rejection
tests already used `%u` (changed in `c0d36f7`, carried forward
unmodified). The shared formatter's real count guard's private test-only
wrapper is out of scope for this pass (not needed by any named consumer);
noted here in case a future consumer needs the return-count overflow
boundary explicitly re-verified after this restructuring.
