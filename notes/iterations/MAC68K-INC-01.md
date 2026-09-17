# MAC68K-INC-01: give cb_file_probe the compat include the Linux build gives it

- Status: in progress
- Base SHA: a9a936d
- Branch: `work/MAC68K-INC-01`
- Hypothesis: `main` is red in Woodpecker and has been for at least eight
  pipelines. `ci` and `mac-automation` succeed; `mac68k` fails at
  `build-mac68k`. The cause is an include-path divergence, not a code defect:
  `tests/libc_file_probe.c` reads `MAXBSIZE`, which is defined only in
  `compat/netbsd/include/sys/param.h`. The Linux rule at `Makefile:848` compiles
  that file with `-Icompat/netbsd/include -Ilibc/include`, while
  `platform/mac68k/CMakeLists.txt:22` gives every probe in its `foreach` only
  `${ROOT}/libc/include`. The two builds enumerate include paths
  independently, so a probe that gains a compat-header dependency compiles on
  Linux and fails only on mac68k.

## Red

- Command: Woodpecker `mac68k` / `build-mac68k`, pipeline 452 step 4716 on
  `work/BUILD-CC-01`, and pipeline 450 step 4700 on `main` at `89ef526`
- Expected failure: `MAXBSIZE` unresolved when the probe is compiled without
  the compat include directory
- Observed failure, identical on both:
  ```
  tests/libc_file_probe.c:61:13: error: 'MAXBSIZE' undeclared (first use in this function)
     61 |         if (MAXBSIZE != 65536) return 118;
  gmake[2]: *** [CMakeFiles/cb_file_probe.dir/...libc_file_probe.c.obj] Error 1
  gmake: *** [Makefile:91: all] Error 2
  ```
  The failure is byte-identical on `main` before this branch existed, so it is
  pre-existing and was not introduced by `BUILD-CC-01`.

## Green

- Focused command: Woodpecker `mac68k` on this branch's pushed SHA. The Retro68
  toolchain is not available locally, so Woodpecker is the only evidence
  available for this change and a local run cannot substitute for it.
- Full command: `make ci` on Linux, to prove the change does not disturb the
  Linux build. The change touches only `platform/mac68k/CMakeLists.txt`.
- Linux Woodpecker: pending
- mac68k Woodpecker: pending
- Guest acceptance, when required: not applicable. This changes an include path
  for a host-side probe object and no runtime behavior.

## Change and review

- Implementation: one `target_include_directories(cb_file_probe PRIVATE
  ${ROOT}/compat/netbsd/include)`, placed beside the existing
  `cb_file_probe` compile definition and commented with the Linux rule it
  mirrors.
- Scope check performed rather than assumed: of the twenty-two probes in the
  `foreach` at line 20, `file` is the only one whose Linux rule passes
  `-Icompat/netbsd/include`, so no other probe needs the same treatment and
  this fix does not merely uncover the next identical failure. Verified by
  comparing each probe's `$(BUILD)/libc_<name>_probe.o` rule against the
  CMake target list.
- ABI, ownership, and cleanup review: no ABI surface, no runtime code, no
  shared libc symbols, no new files in the build.

- Remaining risk or follow-up:
  - **The mac68k target builds none of this milestone's commands.** There is no
    `cb_cat`, `cb_cp`, `cb_mv`, `cb_rm` or `cb_ls` target in
    `platform/mac68k/CMakeLists.txt`; it builds `dirname`, `basename`, `wc`,
    `yes`, `head`, `echo`, `printenv` and the probes. So `cat`, `cp`, `mv`, `rm`
    and `ls` have never been compiled for System 7, and a green `mac68k`
    workflow currently says nothing about them. This is why the workflow kept
    passing while the command set grew: it covers a shrinking fraction of the
    tree. Queued as `MAC68K-CMD-01`; it is compile coverage only and needs no
    guest, so it is not blocked on Basilisk II access.
  - The structural cause remains: two builds enumerate include paths by hand.
    A single shared list, or a mac68k build that fails when a source's include
    set diverges from the Linux rule for the same source, would prevent the
    next instance. Not attempted here because unblocking `main` should not wait
    on a build-system refactor.
  - `main` was merged red at least twice while `mac68k` was failing (`89ef526`
    for CAT-01 and `a9a936d` for CP-01). The gate requirement in `AGENTS.md`
    step 6 is unchanged; what was missing was anyone reading Woodpecker rather
    than a local `make ci`. Raised with the coordinator.
