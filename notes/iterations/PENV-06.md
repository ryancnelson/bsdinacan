# PENV-06: unchanged NetBSD `printenv`

- Status: in progress
- Base SHA: `28b76864a1147c68ca8715d22966e0972e61f092` (local `work/PENV-05`,
  reviewed, per explicit dependency-base override)
- Branch: `work/PENV-06`
- Hypothesis: the exact pinned NetBSD source will compile unchanged and run
  entirely through the cannedBSD libc and runtime built across
  `PENV-01`..`PENV-05`.

## Red

- Command: `bash tests/test_netbsd_source.sh` (after a normal build, then
  with the pinned file moved aside) in the Alpine 3.22 container matching
  the Woodpecker agent image
- Expected failure: the source/import boundary fails while the file is
  absent.
- Observed failure: `FAIL: pinned NetBSD printenv source is missing:
  upstream/netbsd/usr.bin/printenv/printenv.c`, confirmed by temporarily
  moving the already-fetched, hash-verified file aside and rerunning the
  boundary script alone.

## Green

- Focused command: `make LDLIBS=-lucontext test`
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- Linux Woodpecker: #73 success on `55fa557`.
- mac68k Woodpecker: #73 success on `55fa557`, after fixing the first pushed Mac link.
- mac-automation Woodpecker: #73 success on `55fa557`.
- Guest acceptance: coordinator-owned; not performed in this session.

## Change and review

- Source verification: fetched
  `https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/usr.bin/printenv/printenv.c`
  and confirmed its SHA-256 (`d355c07f...de974db`) matches the backlog's
  pinned hash exactly before importing it byte-for-byte. No line of the
  imported file was touched.
- Implementation: `commands/printenv_module.c` (new) supplies the
  `CB_LIBC_PROGRAM` descriptor, registered in `cb_register_base_programs`
  alongside `wc`/`yes` (`src/programs.c`) — `printenv` is a real, always-
  available command, not test-only scaffolding. `libc/include/sys/types.h`
  (new) is a minimal import-only shim (`stddef.h`/`stdint.h` only) satisfying
  the source's `#include <sys/types.h>`; nothing from it is otherwise
  exercised, and no new libc surface was added — every interface the source
  calls (`environ`, `exit`, `getopt`/`optind`, `printf`/`fprintf`/`stderr`,
  `errx`, `strchr`/`strlen`/`memcmp`) already existed from `PENV-01`
  through `PENV-05` and PENV-20/21/25.
- **Toolchain concession, documented in `UPSTREAM.md`**: the pinned source's
  local `extern char **environ;` inside `main` — a redundant, harmless
  redeclaration on a real system where `environ` is a plain extern variable
  — expands, once `environ` is cannedBSD's task-local
  `(*cb_libc_environ_location())` macro, into a declarator that both GCC and
  Clang parse as an *unprototyped redeclaration of
  `cb_libc_environ_location` itself*. This trips `-Wstrict-prototypes`
  (part of both compilers' `-Wall` for C), fatal under this project's
  `-Werror`. Verified empirically outside the tree before touching the
  Makefile (a standalone reproduction with the exact macro shape and
  function signature). `-Wno-strict-prototypes` is added to
  *only* `printenv.c`'s two compile rules (the command object and the
  `analyze` target's `-fsyntax-only` pass); no other translation unit loses
  `-Wstrict-prototypes` coverage. This is a consequence of `PENV-01`'s
  task-local `environ` design meeting one specific pre-existing coding
  idiom in this one pinned file — not a defect in the source and not a
  broadening of the runtime ABI, the same class of documented,
  narrowly-scoped adapter already recorded for `strcmp`/`memcpy`/`memmove`.
- Behavioral coverage (`tests/test_printenv_behavior.sh`, new, exercised
  entirely through the shell against the real registered command — no
  direct unit-level probe needed since every required behavior is
  externally observable): named lookup (`printenv PATH` → `/bin`), missing
  variable (no output, exit 1), an empty-valued variable (`export
  EMPTYVAR=; printenv EMPTYVAR` → an empty line), enumerate-all (`printenv`
  with no arguments lists every boot-environment entry), the `=`-in-name
  fatal diagnostic (`printenv: Invalid environment variable FOO=bar` on
  `stderr`, no `stdout`, status 1), the too-many-arguments usage diagnostic
  (`Usage: printenv [name]` — no program-name prefix, since the pinned
  source's `usage()` calls `fprintf` directly, not `errx`), pipeline use
  (`printenv PATH | cat`), and output redirection (`printenv PATH >
  file`).
- Source/import boundary (`tests/test_netbsd_source.sh`, extended
  alongside the existing `yes` checks): pins the hash and provenance
  record, confirms the separately compiled object defines the renamed
  `cb_printenv_main` entry point and not host `main`, and rejects host
  imports of `environ`/`getopt`/`errx`/`exit`/`memcmp` in favor of their
  private `cb_libc_*` counterparts (representative coverage of the
  interfaces the source actually calls, matching the existing single-symbol
  precedent set by `yes.c`'s `puts` check, scaled to this file's larger
  surface).
- Documentation: `UPSTREAM.md` gains the full provenance entry (repository,
  revision, path, hash, RCS identifier, license, and the toolchain
  concession above).
- The first pushed change omitted Mac build registration. Woodpecker exposed
  the missing symbol; `55fa557` added the printenv source/module to the Mac
  CMake target and all three #73 workflows passed. Linux-only validation
  could not establish that separate build target was complete.
- Integration adds named, missing, empty-valued and invalid-name printenv
  commands to the shared Linux/Mac acceptance definitions. Exact combined
  guest execution remains pending until recorded by the coordinator.
- Remaining risk or follow-up: no speculative libc surface was added — the
  dependency ladder in `CAPABILITY-MAP.md` already anticipated exactly this
  set of interfaces. Guest acceptance under AGENTS.md step 7 is outstanding,
  coordinator-owned.
