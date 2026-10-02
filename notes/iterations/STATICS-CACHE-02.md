# STATICS-CACHE-02: repair retained command buffer lifecycle

**Subsequent quality repairs:** this note preserves the original investigation.
FORMAT-SIZE-01 now handles the measured stream `%zu` warning; STATICS-TYPES-01
replaces the erased ls array/callback declarations with exact types and a typed
storage accessor. FORMAT-PRI64-01 matches format macros to actual integer types:
equal widths do not make different va_arg types compatible. These repairs are
reviewed candidates with exact CI success, still awaiting guest qualification.
The qualified option matrix below incorporates LS-P-OPERAND-01's measured limits.

- Status: implementation and Linux evidence complete; Woodpecker, fresh Mac
  guest and native Solaris qualification are coordinator gates (below).
- Base SHA: `b2d003b6820d6c19b47885986e8271f5b2d049fe` (STATICS-RESET-01),
  plus STATICS-REPRO-GATE-02 `1e3b58bde48fa3318683aeb94a2a386959206e08`
  cherry-picked unchanged as `749866e` (test-only; consumed, not edited).
- Branch: `work/STATICS-CACHE-02`
- Hypothesis: keeping function-local command caches alive by retaining task
  allocations (CB_EXECUTOR_PERSISTENT_HEAP) does not give each task its own
  cache. A retained `buf` outlives its size, its kernel and its task, and
  interleaved tasks share it. Owning every cache static as a per-task slot,
  restored to compiled defaults between runs, and releasing allocations at
  task exit repairs all of these.

## Toolchain inspection before choosing the fix

`nm -P` on the pinned sources' objects:

| Compiler | raw_cat `buf` / `fb_buf` | fastcopy `bp` / `blen` | printcol `array` / `lastentries` |
|---|---|---|---|
| Alpine GCC 14.2.0, -O0 and -O2 | `buf.1` / `fb_buf.0` | `bp.0` / `blen.1` | `array.0` / `lastentries.1` |
| Alpine Clang 20.1.8, -O0 and -O2 | `raw_cat.buf` / `raw_cat.fb_buf` | `fastcopy.bp` / `fastcopy.blen` | `printcol.array` / `printcol.lastentries` |
| Retro68 m68k GCC 16.1.0 (pinned image `459dd3ea…`), -O0/-Os/-O2 | `buf.1` / `fb_buf.0` | `bp.0` / `blen.1` | `array.0` / `lastentries.1` |

The GCC suffix is a per-file counter, not random, but it is still the
compiler's choice. `tools/globalize-function-static.sh` therefore finds the
one local data symbol named `VAR.N` or `FUNC.VAR` and renames it with
objcopy. It fails the build if there are zero or several matches, and does
nothing when the symbol is already renamed (CMake PRE_LINK reruns on relink).
Solaris GCC 3.4.6 and the Solaris `nm` were not inspected: that rig is not
this worker's.

## Red

Pinned Linux image `tribblix-woodpecker-agent:3.18.0` (image ID
`sha256:7618701ca718…`, Alpine, GCC 14.2.0), on biggie, network disabled.
The new `tests/statics_cache_probe.c` and its `test_core` wiring were
applied to the unfixed runtime tree (`749866e`'s `src`, `Makefile`,
`platform`):

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --statics-cache-case N   # N = 0..6
```

The probe's host allocator adds a trailing canary to each block and holds
released blocks, poisoned, in quarantine until the scenario ends. Overflow
and use-after-free therefore fail in the normal (non-sanitizer) build.

| Case | Scenario | Unfixed result |
|---|---|---|
| 0 | `cat -B 2048 f; cat -B 4096 f` (3002-byte f) | SIGSEGV, exit 139 |
| 1 | two kernels in one process, each `cat -B 2048 f` | status 24: first kernel's released buffer written by the second |
| 2 | two kernels, each `ls /tmp` twice | status 33: second kernel lists one name per line (termwidth default lost) |
| 3 | `cat big \| cat` (12008 bytes; default 1024-byte fb_buf) | status 43: pipeline output differs from input |
| 4 | `cat -B 4099 big \| cachecheck` | status 51: cat's buffer still allocated after cat exited |
| 5 | `-B 4099` malloc fails once, then `cat -B 4099` again | status 64: second cat reused the 1024-byte fallback instead of allocating |
| 6 | fail each Nth allocation made by sh/cat during a cat session | SIGSEGV, exit 139 |

Earlier, before the probe existed, the unfixed binary showed the pipeline
defect directly. `cat /tmp/big` hashed `bbb3a566…`, while `cat /tmp/big |
cat` hashed `12fda201…` and `cat -B 2048 /tmp/big | cat -B 4096` hashed
`437f1570…`, all with exit status 0. Two cat tasks in one pipeline shared
`fb_buf` (and `buf`/`bsize`), so a writer blocked on a full 4096-byte pipe
had its pending data overwritten by the reader. The same sharing exists on
`origin/main`, which has no reset wrapper.

Case 5's first formulation compared only output. The unfixed runtime
passed it in the plain build, because the overflow into fb_buf happened not
to disturb the output. The case was then tightened to require the second
cat's own `-B` allocation, and this red was re-observed. Case 5 checks only
the `cat: malloc, using ` prefix of the warning. This libc (base and main)
does not format `%zu` and prints nothing from that conversion onward. That
is recorded as a follow-up, not fixed here.

## Green

- Focused command: `./build/test_core --statics-cache` printed `statics
  cache tests passed` (exit 0) in the same pinned image. Every case also
  passes individually.
- Mac compile: in the pinned Retro68 image, `cmake` + `cmake --build` and
  `check_code_resources.py` passed ("single executable segment verified").
  `m68k-apple-macos-nm` on `CannedBSD.code.bin.gdb` shows all seven renamed
  globals (`cb_cat_bsize`, `cb_cat_raw_cat_buf`, `cb_cat_raw_cat_fb_buf`,
  `cb_mv_fastcopy_bp`, `cb_mv_fastcopy_blen`, `cb_ls_printcol_array`,
  `cb_ls_printcol_lastentries`). CMake found `CMAKE_NM` as
  `/Retro68-build/toolchain/bin/m68k-apple-macos-nm`. This is a local build,
  not the Woodpecker artifact, and nothing ran in the guest.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in the pinned
  image (ID `sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`),
  run from a disposable full clone at `9409b626cd6773ed30f40c62ae02ca2a16bbbe9a`.
  Exit 0 in 5m34s; log SHA-256
  `d5a10d33c1f64d3f2a253251e2e08f9e88310087a21873b4de1d4b58d47d585b`. The log
  shows:
  - Mac guest protocol tests (18) and `test_statics_repro_gate.py` (5)
    passed.
  - Publication hygiene passed.
  - The normal build passed `all core tests passed`, which includes the
    probe, plus both pinned NetBSD source boundaries, every behavioral
    matrix, and `test_statics_repro.sh`.
  - The ASan+UBSan build passed the same suite.
  - Build-mode isolation passed, the second clean test passed, and the GCC
    analyzer passed.

  The pushed commit differs from `9409b62` only by this paragraph.
- Linux/mac68k Woodpecker: reported in the handoff for the pushed commit.
- Guest acceptance: required (runtime and command behavior change) and not
  performed by this worker; the coordinator owns the serialized Basilisk
  slot. Native Solaris qualification is likewise outstanding.

## Change and review

- Implementation:
  - `src/static_reset.c`: cat gains slots for `bsize`, `buf` and
    `fb_buf[1024]`, ls for printcol's `array` and `lastentries`, and mv for
    fastcopy's `bp` and `blen`.
  - After each run the live slots are restored to the program's captured
    compiled defaults instead of zero. That keeps later kernels' default
    capture correct: termwidth stays 80 and lastentries -1.
  - The heap-retention mechanism is removed completely: capability bit
    `CB_EXECUTOR_PERSISTENT_HEAP`, `cb_program.persistent_allocations`,
    `cb_executor_supports_persistent_heap()`,
    `cb_task_release_allocation_list()`, and the core.c splice.
    `src/core.c` and `src/executor.c` are back to their pre-STATICS-RESET-01
    text, so every task's allocations are released at exit again.
  - Build: the new helper runs in the Makefile and mac68k CMake rules for
    cat.c, mv.c and print.c, and cat's `bsize` is added to the existing
    objcopy rename. print.c is now compiled at -O0, like the other renamed
    objects, for the same Clang+ASan global-narrowing reason.
- ABI, ownership, and cleanup review:
  - No public ABI change. The removed capability bit and fields were
    internal (`src/internal.h`), and nothing outside this wrapper set them.
  - Slot sizes come from `sizeof` on declarations whose types match the
    definitions:
    - `size_t bsize`;
    - `char *buf`;
    - `char fb_buf[BUFSIZ]` with BUFSIZ = 1024 (`libc/include/stdio.h`);
    - `char *bp`;
    - `blksize_t` = `int32_t blen`;
    - `int lastentries`;
    - `FTSENT **array`, declared as `void *` because this file cannot see
      FTSENT; only the storage size matters to a slot.
  - An exited task's slots are discarded, never saved. Its allocations,
    including any cache buffer, are released at exit (case 4 observes this
    from the pipeline reader after EOF, before the session ends).
  - Allocation-failure paths in `instance_create` are unchanged and are
    swept by case 6 (the shell side of the spawn is included).
  - Cost: cat now copies about 1.1 KB of slot state in and out on each
    resume. That is bounded and applies only to cat.
- Shared symbols touched: `cb_program.static_defaults` (comment only),
  `cb_task_release_allocation_list` and `cb_executor_supports_persistent_heap`
  (removed), `CB_EXECUTOR_PERSISTENT_HEAP` (removed), the new renamed
  globals listed above, and the new build variable `NM` /
  `GLOBALIZE_FUNCTION_STATIC`. No libc symbol and no shared header under
  `include/` or `libc/include/` changed.
- Pinned sources: no file under `upstream/` changed; `test_netbsd_source.sh`
  hashes are part of the full gate.
- Documentation: this note; comments in `src/static_reset.c`,
  `src/internal.h`, `Makefile`, `platform/mac68k/CMakeLists.txt`,
  `tests/test_core.c` and `tests/test_file_manipulation_session.sh`.
- Not edited: `tests/test_statics_repro.sh` and
  `tests/test_statics_repro_gate.py` (STATICS-REPRO-GATE-02's), and
  `tests/libc_fclose_stdout_probe.c` (being extracted to TEST-ENTRY-01 by
  the coordinator).
- Remaining risk or follow-up:
  - Solaris: the helper needs `nm -P` output and GNU `objcopy`. The
    existing renames already need GNU objcopy there, but Solaris 9's
    native `nm` has not been checked. If it lacks `-P`, run `make NM=gnm`
    (or whatever GNU nm is called on the rig).
  - cp `utils.c` copy_file()'s `static char buf[MAXBSIZE]` (64 KB) is still
    shared by all cp tasks. It is safe only while the write that drains it
    cannot yield, which holds for ramfs files but is unverified for pipe or
    console destinations. Not in this task's scope.
  - mv `fastcopy()` is still unreachable (single mount, no EXDEV), so its
    slots are covered by construction, not by a behavioral test.
  - libc `%zu` formatting is missing (see case 5).
  - Case 4 observes release at the reader's EOF. It shows the buffer is
    gone before the session and kernel end, but does not separately prove
    that release happened before the shell reaped cat.
  - Destroying a kernel while a cat task is suspended mid-pipeline is not
    separately exercised here. The live slots hold defaults in that state
    by construction.

## Round 2: main merge, review blocker, LS-02 audit

Assigned after #505 by the coordinator: merge current `origin/main` into this
branch, fix the independent review blocker, audit the LS-02 import, record an
option matrix, and run one complete final CI.

### Merge

`origin/main` `9d13844` was merged first, then `7a8b0fb` (TEST-ENTRY-01's
fclose probe fix, identical to this branch's copy, so it merged cleanly).
Conflicts and how they were resolved:

- **Shared-symbol collision, `cb_libc_mbrtowc`.** WC-02 (main) and LS-02
  (this branch) both implemented it, with different `mbstate_t`/`wint_t`
  types. Main's definition and types were kept (unsigned, guarded `wint_t`
  and `mbstate_t` in `cannedbsd/libc.h`); LS-02's copy was deleted. LS-02's
  `wcrtomb`, `iswprint` and `wcwidth` were retyped to match.
- **Formatter.** WC-02's separate `%u`/`%lu`/`%llu` branch in
  `format_output` became unreachable after LS-02's unified parser (which
  consumes the length modifier first), so it was removed.
- `sys/types.h`, `programs.c`, the Makefile/CMake object lists, the
  format-probe table, and the source-pin tests were unioned.
- The session, mkdir and file-manipulation expectations were rewritten from
  the built binary for upstream ls and upstream wc together, and each was
  checked by hand.

### Review blocker: `printfcn`/`sortfcn`

ls.c chooses `printfcn`/`sortfcn` in main but calls them only after an fts
traversal that can yield. They are now slots, renamed by objcopy (Makefile and
CMake), together with print.c's `now` and cat.c's `filename`, which follow the
same assign-then-yield pattern.

- New control: `test_core` `lsmixedinterleave` runs `ls -1r` and `ls -m`
  forced to interleave in readdir, and requires each task's exact output.
- Red, before the slots: `./build/test_core --ls-interleave` exited 1 with
  child status 927 (reported as 159). The `-m` task printed a reverse,
  one-per-line listing (`beta-two\nbeta-one`), i.e. the other task's
  comparator and display routine.
- Green after: `ls interleave tests passed`.

### ls fixes found by the audit

Each fix has a red observed first with the expected NetBSD result.

1. **`ls -Ra` walked out of the tree.** FTS_SEEDOT's `.` and `..` were
   classified as ordinary directories and descended into, producing "directory
   causes a cycle" and listings of `/bin` and `/home`. They are now
   `FTS_DOT`: stat'd for `-la`, never descended into or cycle-checked.
2. **`ls -P` printed `./x/x`.** ls prints `fts_path "/" fts_name`, and NetBSD
   `fts_build()` leaves the parent's path in a listed child's `fts_path`.
   Children in a `fts_children()` list now read their parent path;
   `fts_read()` restores the full path when it returns that entry.
3. **Operands were printed by basename** (`ls /bin/wc` gave `wc`, `ls -d /`
   printed an empty line). NetBSD `fts_open()` keeps the whole argument in
   `fts_name` (`fts.c:165`), and only `fts_read()`'s `fts_load()` trims it,
   keeping `/` as `/`. Both rules are now implemented. Two existing
   expectations that asserted the basename (`ls /bin/wc`, and the ENOENT
   diagnostic in the ls and session suites) were replaced, not deleted.
4. **`-h` failed with ENOSYS.** The pinned, unchanged NetBSD
   `lib/libc/gen/humanize_number.c` (`2f311138…`) is imported, with NetBSD's
   own `HN_*` values. LS-02's `HN_AUTOSCALE 0` would have silently disabled
   autoscaling. Supporting additions: `PRId64` and a C-locale `localeconv()`
   (`decimal_point "."`, `thousands_sep ""`, `grouping ""`); naming any other
   `lconv` member fails to compile. The previous test asserted the ENOSYS
   failure; it now asserts `2.9K`, `3B` and `total 0B`. The ENOSYS behaviour
   is the prior evidence here, not a separate test-first run.
5. **`%'` inserted commas.** POSIX groups with the locale's `thousands_sep`,
   which is empty in the C locale (the only locale here), so `ls -lM` now
   prints `1400000`, as NetBSD printf does under `LANG=C`. This contract
   change replaces the `formatprobe c` assertion (`1,234,567` became
   `1234567`, count 21 became 19).

### 32-bit format and calling-convention audit

The printf, fprintf, snprintf, warn, warnx, err and errx veneers were
redeclared with `format(printf)` through `-include`, audit-only and never part
of the build. ls.c, print.c, util.c and cmp.c were then compiled `-Wformat=2`
with Retro68 m68k GCC 16.1 (ILP32) and Alpine GCC 14.2 (LP64). Every warning
is a same-width alias with no va_arg width mismatch:

- m68k: `%u` receives `uint32_t`, which newlib defines as `unsigned long`
  (both 32 bits), at ls.c 581, 588 and 645.
- LP64: `%llu` (`PRIu64`) receives `uint64_t`, which is `unsigned long`
  (both 64 bits), at print.c 146 and 377.

`time_t` is `uint32_t` in this libc, so dates beyond 2106 are unrepresentable.

### ls option matrix (pinned NetBSD ls, this runtime)

"Tested" names a case in `tests/test_ls_behavior.sh` (or `test_core`);
"observed" means checked in the audit runs but not asserted by a test.

| Option | Status | Notes |
|---|---|---|
| (none), `-C` | works, tested | 80-column layout; the console reports no `TIOCGWINSZ` |
| `-1` | works, tested | also the default when stdout is a pipe |
| `-A` | works, observed | implied anyway: the runtime reports uid 0, and NetBSD root implies `-A` |
| `-a` | works, tested | `.`/`..` included |
| `-B` `-b` `-q` `-w` | work, tested | `-q` is the default on the console, raw bytes go to a pipe |
| `-c` `-u` | work, observed | FS-STAT-01 ctime/atime |
| `-d` | works, tested | including `/` and trailing-slash operands |
| `-F` `-p` | work, tested | `*` on command nodes, `/` on directories |
| `-f` | works, tested | unsorted, `.`/`..` first |
| `-g` `-n` `-o` | work, observed | uid/gid are always 0; `-o` shows `-` (no file flags) |
| `-h` | works, tested | pinned humanize_number |
| `-i` | works, observed | real RAMFS inode numbers |
| `-k` `-s` | work, tested | **st_blocks is always 0 (RAMFS)**, so block counts and `total` are 0 |
| `-L` | accepted | RAMFS has no symlinks, so it has no effect |
| `-l` | works, tested | mode/nlink/uid/gid/size are real; the date column is wall-clock |
| `-M` | works, tested | C locale: no separator inserted |
| `-m` `-x` | work, tested | upstream `-x` pads the last column |
| `-O` | upstream behaviour, observed | suppresses headers and totals only |
| `-P` | directory children tested; root operands unresolved | File and `-d` directory operands duplicate paths; mixed operands inherit this limitation. See LS-P-OPERAND-01. |
| `-R` | works, tested | with and without `-a` |
| `-r` `-S` `-t` | work, tested | mtime ties fall back to name order |
| `-T` | works, observed | full date and time |
| `-W` | accepted | RAMFS has no whiteouts |
| `-X` | **unsupported, tested** | fails with `ls: function not implemented`, status 1 (`FTS_XDEV`; see FTS-XDEV-01 in fts.h) |
| invalid | tested | upstream usage message, status 1 |

Other limits:

- Diagnostics use this libc's lowercase `strerror` text (for example "no such
  file or directory"), which differs from NetBSD's capitalization.
- `-P` file and `-d` directory root operands duplicate paths in the measured
  candidate. LS-P-OPERAND-01 records eight fixtures and a controlled allocation
  experiment with pinned FTS. Native NetBSD behavior remains unmeasured; the
  experiment establishes initial-buffer dependence, not a replacement contract.
- No test compares output against a real NetBSD host. Every expectation above
  comes from reading the pinned sources.

### Round 2 shared symbols and headers

- libc:
  - added: `cb_libc_localeconv`, `struct cb_libc_lconv`;
  - replaced: `cb_libc_humanize_number`, now the pinned upstream object
    instead of the ENOSYS stub;
  - removed: LS-02's duplicate `cb_libc_mbrtowc`;
  - retyped: `cb_libc_wcrtomb` and `cb_libc_iswprint`;
  - changed: `format_output`'s `'` flag.
- Headers:
  - `libc/include/stdlib.h`: `HN_*` values and `humanize_number`;
  - `libc/include/util.h`: now includes stdlib.h;
  - `libc/include/inttypes.h`: `PRId64`;
  - `libc/include/locale.h`: `lconv`, `localeconv`;
  - `libc/include/wchar.h`, `wctype.h`: merged;
  - `libc/include/fts.h`: `FTS_DOT`, documented `fts_path` meaning;
  - `include/cannedbsd/libc.h`.
- fts: `cb_libc_fts_children` (dot entries, parent path, operand names) and
  `cb_libc_fts_read` (restores the path).
- New renamed command globals: `cb_ls_printfcn`, `cb_ls_sortfcn`,
  `cb_ls_print_now`, `cb_cat_filename`.
- No change to `include/cannedbsd/abi.h`, the signal veneer, or anything
  tee-related.
