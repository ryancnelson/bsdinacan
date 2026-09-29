# LS-ROOT-ORDER-01: sort directory operands during traversal

- Base: `5ba7741d5228cf9765c1bc5e9d723582633ffca3` (STATICS-CACHE-02 round 2).
- Branch: `work/LS-ROOT-ORDER-01`; separate sibling worktree; the cache
  worker's checkout and upstream sources were not edited.
- Hypothesis: retaining the sorted root preview and consuming its order in
  `fts_read()` makes ls directory sections follow the selected comparator.

## Red

Added actual ls cases to `tests/test_ls_behavior.sh`, then copied those tests
onto an unmodified base source archive in an isolated Linux directory. Used
`tribblix-woodpecker-agent:3.18.0`, image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
network disabled, with `/bin/sh` as the container entrypoint:

```
make LDLIBS=-lucontext -j4
tests/test_ls_behavior.sh
```

The build succeeded; the new normal-order test failed, exit 1:
`directory operands sort by name stdout mismatch`. After creating `/tmp/z/z`
and `/tmp/a/a`, `ls -1 /tmp/z /tmp/a` printed the z section then the a section;
the expected comparator order was a then z.

## Change

`libc/cb_fts.c` retains the root preview and a cursor into its remaining entries.
Traversal creates its independently owned entry from the next preview's path,
then advances the cursor only after successful allocation. A comparator caller
starting with `fts_read()` lazily creates the same preview; comparator-free
callers without a preview retain streaming traversal. The retained preview is
released by the existing close cleanup. No ABI, public header, shared symbol,
command static slot, or imported source changed.

The behavioral matrix tests normal and reverse directory-section ordering,
size-sort ties (RAMFS directory sizes are zero), and time sorting in both
directions. Time cases compare traversal with a preceding `ls -d` on the same
unchanged filesystem, so tied and distinct real timestamps are both valid.
The existing `ls /tmp /home` header test was corrected from argument order to
name order; its two section headers and contents remain asserted.

## Green and qualification

- In the same isolated pinned Linux image, `tests/test_ls_behavior.sh` passed
  (`ls behavioral matrix passed`, exit 0).
- `make LDLIBS=-lucontext test` passed, exit 0.
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` and exact pushed-commit
  Woodpecker ci/mac68k/mac-automation results will be supplied in the handoff.
- Fresh Mac guest and native Solaris acceptance are required and remain
  coordinator gates. This worker did not operate either guest.
- The bounded change repairs root operand ordering; it is not a claim that
  every ls behavior or every fts API corner case has been qualified.
