# STATICS-TYPES-01 — exact types for ls reset slots

- Assigned base: `61beeac0beef50a086622e6ffe734e10e15b5273`.
- Branch: `work/STATICS-TYPES-01`, isolated sibling worktree.
- Hypothesis: strict compilation of source-derived declaration pairs rejects
  three incompatible ls state-slot declarations, and exact declarations remove
  the mismatch while preserving command interleaving and cache isolation.

## Scope and implementation

The old reset wrapper declared `FTSENT **array` as `void *`, and both
`void (*printfcn)(DISPLAY *)` and
`int (*sortfcn)(const FTSENT *, const FTSENT *)` as `void (*)(void)`.
It only took their addresses and copied bytes; calls remained correctly typed
inside pinned ls.c. This is a declaration and pointer-representation portability
repair, not a demonstrated wrong-signature callback call or corruption fix.

Array and sortfcn now use the private `struct cb_ftsent` tag, forward declared
without including libc headers in core. DISPLAY is anonymous in pinned ls.h,
so the small command-side `commands/ls_static_slot.c` bridge uses that real
header and supplies printfcn's storage address and actual sizeof. The existing
executor-registration function initializes its slot before any execution can
capture defaults. Repeated registration assigns the same address and size;
it does not reset live command state. No new runtime framework or ABI changes.
Linux and Mac builds compile and link the bridge with private command headers.

Shared symbols touched: corrected declarations for `cb_ls_printcol_array`,
`cb_ls_sortfcn`, `cb_ls_printfcn`; added the narrow `cb_ls_printfcn_slot`
bridge declaration in `include/cannedbsd/ls_state.h`. No libc implementation
symbols changed. All pinned source bytes remain unchanged. SHA-256:

- ls.c: `385a3c3f495913a04127fe52e082030c57e43e5d1bacf9299b2d7b5b2597787a`
- print.c: `012336e4f206483f4aaf31a7380889aa677f66b53fc388442766dccacd1ee8a8`
- ls.h: `50610b1281ff61a6171de9c73dab7ee9861e0ab0fbd66118242786abb325dc34`

## Evidence

Before implementation, `python3 tests/test_statics_types.py` on Apple Clang 17
returned 1 with three independent `redeclaration ... with a different type`
errors naming array, sortfcn and printfcn. The check extracts the real pinned
static declarations, converts their linkage/names for comparison, and compiles
against the actual owned declarations with C99 strict warnings. It compares
compatible types, not pointer widths. The same check now passes for all three
on Apple Clang and Linux GCC and is required by `make test`.

Focused Linux validation used image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`
with network disabled and an isolated temporary source copy:

- `make -j4 LDLIBS=-lucontext build/bsdinacan build/test_core
  check-statics-types check-architecture check-build-parity`: passed.
- `build/test_core --ls-interleave`: passed both existing cases.
- `build/test_core --statics-cache`: passed all existing cases.
- `PROGRAM_PATH=./build/bsdinacan tests/test_statics_repro.sh`: five passed,
  zero failed; mv fastcopy remains unexercised, as before.
- `git diff --check`: passed.

Two setup errors were corrected before claiming results: Docker initially used
its agent entrypoint instead of bash, and the shell repro initially defaulted
to an unbuilt sanitizer path. Neither is red product evidence. The successful
focused run above used the explicit built binary. No full local CI rerun was
performed; full sanitizer and platform gates are delegated to exact pushed-commit
Woodpecker ci, mac68k and mac-automation. Their final results and independent
review are reported in the handoff.

No guest was operated and main was not changed. Fresh Mac artifact execution
and native Solaris qualification remain coordinator-owned acceptance gates.
