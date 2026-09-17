# BUILD-CC-01: detect the memset idiom flag by probing, not by compiler name

- Status: in progress
- Base SHA: 89ef526
- Branch: `work/BUILD-CC-01`
- Hypothesis: the guard selecting `-fno-tree-loop-distribute-patterns` tests
  `$(findstring clang,$(CC))`, which is a test of the compiler's *name*. The
  Clang driver is installed as plain `cc` on macOS and FreeBSD and on Linux
  distributions that point `cc` at Clang, so on those hosts the guard does not
  fire, the GCC-only flag is passed to Clang, and Clang rejects it outright.
  The build then fails at the first `memset.c` rule for a reason unrelated to
  any change under test. The guard is correct today only because the one
  documented Clang invocation, `SANITIZE_CC=clang`, happens to spell the name
  "clang" in `$(CC)`.

## Red

- Command: `make build/netbsd_memset.o` on macOS 25.3.0 with Apple clang 21.0.0
  installed as `cc`
- Expected failure: the GCC-only flag reaches Clang and is rejected
- Observed failure:
  ```
  -fno-builtin-memset -fno-tree-loop-distribute-patterns \
  -DCANNEDBSD_BUILDING_LIBC_MEMSET -c .../memset.c -o build/netbsd_memset.o
  clang: error: unknown argument: '-fno-tree-loop-distribute-patterns'
  make: *** [build/netbsd_memset.o] Error 1
  ```

## Green

- Focused command: expand `MEMSET_NO_IDIOM_FLAGS` under each compiler and
  confirm the flag is present exactly when the compiler accepts it.
  - Alpine 3.22 `cc` (GCC): `MEMSET_NO_IDIOM_FLAGS := -fno-builtin-memset
    -fno-tree-loop-distribute-patterns` — flag still applied, so the
    self-recursion protection this rule exists for is unchanged.
  - Alpine 3.22 `CC=clang`: `MEMSET_NO_IDIOM_FLAGS := -fno-builtin-memset` —
    matches the behaviour of the previous name-based guard on the one path that
    guard handled correctly.
  - macOS `cc` (Clang named `cc`): flag absent, and every object including
    `build/netbsd_memset.o` now compiles. This is the case the old guard got
    wrong.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in Alpine 3.22
  with the package set documented in `CI.md`
- Linux Woodpecker: pending
- mac68k Woodpecker: pending
- Guest acceptance, when required: not required; no runtime, libc, VFS, shell,
  command, or `platform/mac68k` behavior changes. Build-flag selection and
  documentation only.

## Change and review

- Implementation: replaced the name match with a one-shot `$(shell ...)` probe
  that compiles an empty translation unit with the flag and `-Werror`, adding
  the flag to `MEMSET_NO_IDIOM_FLAGS` only when the probe exits zero. `-Werror`
  is part of the probe so a compiler that merely warns about an unknown
  argument is also treated as not accepting it.
- ABI, ownership, and cleanup review: no ABI surface, no runtime code, no new
  shared libc symbols. The only behavioral surface is which flag reaches one
  compile rule, verified above under three compilers.
- Documentation: `AGENTS.md` gains three rules whose absence has already cost
  real merged bugs — symbol-level ownership (six duplicate shared-symbol
  implementations, three of them merged defects), capability checks naming the
  field they gate (`cb_libc_rmdir` used `offsetof(rename)` and was correct only
  by accident of struct layout), and probing for compiler capabilities rather
  than matching names. It also records that nobody works in the primary
  checkout, and that a worktree's `.git` is a file pointing at the parent
  repository, so bind-mounting only the worktree into the Linux container fails
  `check-publication` with `fatal: not a git repository` for a reason unrelated
  to the change under test. `BACKLOG.md` records the first end-to-end
  measurement of the file-manipulation milestone and the four items it exposed.

- Remaining risk or follow-up:
  - macOS native builds remain blocked after this change, independently, at
    `src/host_linux.c`: `getcontext`, `makecontext` and `swapcontext` are
    deprecated on macOS and `-Werror` promotes the deprecation to an error.
    macOS native is not a declared target in `TARGETS.md`, so this is recorded
    rather than fixed. Do not describe this change as unblocking a target; it
    removes a latent name-based detection, nothing more.
  - The comment this change replaces asserted that Clang "does not exhibit the
    bug at its lower optimization level," which was reasoned for the `-O1`
    sanitize variant. Clang at `-O2` is now reachable on a Clang-as-`cc` host,
    and `-fno-builtin-memset` is the only protection it gets there. That
    combination is unverified because no supported target builds it; if macOS
    or another Clang-as-`cc` host ever becomes a target, measure `memset`
    self-recursion at `-O2` before trusting it.
