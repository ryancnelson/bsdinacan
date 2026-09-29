# BUILD-SYNC-01: a gate for silent Linux/mac68k build divergence

- Status: in progress
- Base SHA: e6dcb3d
- Branch: `work/BUILD-SYNC-01`
- Hypothesis: `Makefile` and `platform/mac68k/CMakeLists.txt` describe
  per-source include paths, defines and flags independently, and nothing
  compares them. All three mac68k defects fixed earlier today came from that
  one cause, with three different symptoms, and the ordering is what makes this
  worth a gate rather than three fixes: the compile error announced itself, the
  link error hid behind the compile error, and the third had **no symptom at
  all**. `cb_memset` was compiled for m68k with `-fno-builtin-memset` but
  without the GCC-only `-fno-tree-loop-distribute-patterns` that `UPSTREAM.md`'s
  standing constraint requires. Retro68 is GCC, so loop-idiom recognition can
  rewrite that function's own fill loop into a call resolving back to itself
  through the `__asm__` link-name binding. The result is unbounded
  self-recursion reachable only at runtime, on the one target whose runtime gate
  is offline until a System 7 guest is available again. No build, test,
  sanitizer or analyzer in this project can observe it.

## Red

Each invariant was falsified by reintroducing the exact defect it exists for,
then reverted. The check is green on `e6dcb3d` before and after all three.

- Command: `python3 tests/test_build_parity.py`
- Invariant A, the silent defect. Removed the
  `-fno-tree-loop-distribute-patterns` block from `cb_memset`:
  ```
  FAIL: Linux and mac68k build descriptions diverge
    - upstream/netbsd/common/lib/libc/string/memset.c: the mac68k build passes
      -fno-builtin-memset but never -fno-tree-loop-distribute-patterns.
  ```
- Invariant B, the compile error. Removed `cb_file_probe`'s compat include:
  ```
    - tests/libc_file_probe.c: Linux build adds compat/netbsd/include but the
      mac68k build does not.
  ```
- Invariant C, the link error, now caught statically. Removed
  `commands/cat_module.c` from the mac68k sources:
  ```
    - cb_cat_program is declared in src/programs.c and defined in
      commands/cat_module.c, but that source is not compiled by the mac68k
      build, so the mac68k link will fail with an undefined reference.
  ```

## Green

- Focused command: `make check-build-parity`
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in Alpine 3.22
  with the package set documented in `CI.md`
- Linux Woodpecker: pending
- mac68k Woodpecker: pending
- Guest acceptance, when required: not applicable. No runtime, libc, VFS,
  shell, command or `platform/mac68k` behavior changes; this adds a check and
  changes no compiled output.

## Change and review

- Implementation: `tests/test_build_parity.py`, wired as `check-build-parity`
  and run as the **first** step of `ci`. It needs no compiler and no toolchain,
  so it fails in under a second rather than after a full build.
- Why invariants rather than a flag-set diff: the two builds differ
  legitimately -- `-Os` versus `-O2`, host-specific translation units the
  mac68k build correctly omits -- so a naive comparison would be noise that
  gets suppressed, and a suppressed check is worse than no check. Each
  invariant corresponds to a defect that actually occurred.
- Invariant A is written as "if the Linux build passes any `-fno-builtin-*`
  flag for a source, mac68k must pass it too, and must also carry the
  loop-distribute companion," rather than naming `memset`. The next import
  bound to a compiler-recognized name via the `__asm__` trick is therefore
  covered without editing this file, which matters because `UPSTREAM.md`'s
  recipe is explicit that the x86-64 clearance for `strcpy`, `strcmp`,
  `memcpy`, `memmove` and `memcmp` does not transfer across a compiler or
  optimization level.
- Invariant C resolves each descriptor to its **defining source** rather than
  guessing a filename from the symbol, because this tree has two definition
  forms -- the `CB_LIBC_PROGRAM` macro and an explicit `struct cb_program_v1`
  initialiser -- and two names that do not follow the pattern
  (`cb_netbsdecho_program`, `cb_shell_program`).
- ABI, ownership, and cleanup review: no ABI surface, no runtime code, no
  shared libc symbols, no new compiled artifacts. The only behavioral surface
  is whether `ci` fails.

- Remaining risk or follow-up:
  - This checks the two builds against each other, not either against reality.
    A flag wrong in *both* places passes. That is a deliberate boundary: the
    defects observed were divergences, and asserting a single correct flag set
    would mean encoding a second source of truth here.
  - The Makefile parse attributes a recipe's tokens to every `.c` prerequisite
    of that rule. For the link rules that list many sources this over-attributes
    flags, which can only cause a *false pass* for a source that shares a rule
    with a flagged one, never a false failure. Per-source compile rules, which
    are the ones that matter for all three invariants, have exactly one `.c`
    prerequisite.
  - The `foreach` expansion handles the two loops in the CMake file today. A
    generated-target construct it does not understand would silently contribute
    no flags, which again fails safe toward a false pass rather than a false
    failure. Worth revisiting if that file grows more generation.
  - Not attempted: unifying the two builds behind one declarative source list.
    `BUILD-SYNC-01`'s acceptance deliberately allowed a check instead, because
    catching the next divergence matters more than restructuring either build,
    and the omission set for mac68k is legitimately non-empty.
