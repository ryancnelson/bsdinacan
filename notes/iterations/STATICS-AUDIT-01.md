# STATICS-AUDIT-01: Comprehensive Inventory of Statics and Globals Across Pinned Commands

## 1. Executive Summary

This audit performs an exhaustive survey of file-scope and function-scope `static` variables (as well as file-scope non-static globals) across all pinned NetBSD utilities currently imported into `bsdinacan`, plus `ls` on `work/LS-02`.

### Key Finding on Pointer Statics and Heap Use-After-Free Severity
Previous assumptions held that `ls` (`print.c`) was the only severe heap Use-After-Free case. **This audit discovered two additional task-heap `POINTER` statics in `mv.c` and `cat.c`** that suffer from identical Use-After-Free vulnerabilities across invocations within the same single-process session:

1. **`mv.c` (`fastcopy()` lines 276-277, 292)**:
   - `static char *bp;` guarded by `static blksize_t blen;`
   - Initialized via `malloc(blen = sbp->st_blksize)` on the first copy. At task exit, `task_release_allocations()` frees `bp`.
   - On a second invocation of `mv` in the same session, `blen` remains non-zero (`!blen` is `false`), so `malloc()` is skipped. The runtime performs `read(from_fd, bp, blen)` and `write(to_fd, bp, nread)` directly on the **freed heap pointer `bp`** (Use-After-Free).
2. **`cat.c` (`raw_cat()` lines 284, 302)**:
   - `static char *buf;` guarded by file-scope `static size_t bsize;`
   - When `bsize > sizeof(fb_buf)` (e.g. via `-B <size>` or filesystem `st_blksize > BUFSIZ`), `buf` is allocated via `malloc(bsize)`. At task exit, `task_release_allocations()` frees `buf`.
   - On a second invocation of `cat` in the same session, `buf != NULL` (stale address), so `read(rfd, buf, bsize)` reads directly into the **freed heap pointer `buf`** (Use-After-Free).
3. **`ls` (`print.c` `printcol()` lines 214-215, 248)**:
   - `static FTSENT **array;` guarded by `static int lastentries = -1;`
   - Allocated via `realloc(array, ...)`. At task exit, `task_release_allocations()` frees `array`.
   - On a second invocation of `ls`, if `entries <= lastentries`, `realloc()` is skipped and elements are written to the **freed heap array** (Use-After-Free). If `entries > lastentries`, `realloc()` is invoked on a freed pointer (heap corruption).
4. **`cat.c` (file-scope line 67)**:
   - `static const char *filename;`
   - Holds `*argv++` from the task-scoped `argv` pool allocated by the shell/executor and freed at task exit by `task_release_allocations()`. Overwritten on entry in `cook_args()`/`raw_args()`, but retains a dangling pointer between runs.

---

## 2. Classification Taxonomy

- **`SCALAR`**: Integers, booleans, enums, flag values, counters, file descriptors. A stale value causes incorrect logic, state leakage across commands, or premature loop/condition exit.
- **`BUFFER`**: Static character or integer arrays (`char buf[...]`, `int stack[...]`). Storage remains valid in the data/BSS segment across invocations; contents may be stale.
- **`POINTER`**: Variables holding memory addresses (`void *`, `char *`, function pointers). If pointing to task-scoped allocations freed by `task_release_allocations()`, a stale value causes a **Use-After-Free**. If pointing to static code/text, harmless.
- **`STRING`**: Static string literals (e.g. `sccsid`, `empty[] = ""`). Harmless.

---

## 3. Comprehensive Inventory Table

| Utility | File & Location | Identifier | Scope | C Type | Class | Allocation Source & Lifecycle | UAF Risk |
|---|---|---|---|---|---|---|---|
| **cat** | `cat.c:64` | `bflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `eflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `fflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `lflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `nflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `sflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `tflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:64` | `vflag` | File-scope | `static int` | `SCALAR` | BSS | None (Wrong flag state) |
| **cat** | `cat.c:65` | `bsize` | File-scope | `static size_t` | `SCALAR` | BSS | None (Retains buffer size) |
| **cat** | `cat.c:66` | `rval` | File-scope | `static int` | `SCALAR` | BSS | None (Retains exit code) |
| **cat** | `cat.c:67` | `filename` | File-scope | `static const char *` | `POINTER` | Points to string literal `"stdin"` or `*argv++` (task-scoped `argv` pool freed at task exit) | **Dangling pointer** (overwritten on entry in `cook_args`/`raw_args`) |
| **cat** | `cat.c:284` (`raw_cat`) | `buf` | Function-scope | `static char *` | `POINTER` | `malloc(bsize)` when `bsize > BUFSIZ` (freed by `task_release_allocations()` at task exit) | **YES (Use-After-Free on 2nd invocation when malloc was used)** |
| **cat** | `cat.c:285` (`raw_cat`) | `fb_buf` | Function-scope | `static char [BUFSIZ]` | `BUFFER` | BSS segment storage (valid across tasks) | None |
| **cp** | `cp.c:86` | `empty` | File-scope | `static char []` | `STRING` | Data segment storage (`""`) | None |
| **cp** | `cp.c:87` | `to` | Global | `PATH_T` (struct) | `BUFFER` / `POINTER` | Global struct; `p_end` and `target_end` point into internal `to.p_path` | None (Pointers stay within global struct) |
| **cp** | `cp.c:89` | `myuid` | Global | `uid_t` | `SCALAR` | BSS | None |
| **cp** | `cp.c:90` | `Hflag, Lflag, Rflag, Pflag, fflag, iflag, lflag, pflag, rflag, vflag, Nflag` | Global | `int` (11 vars) | `SCALAR` | BSS | None (Flags persist across runs) |
| **cp** | `cp.c:91` | `myumask` | Global | `mode_t` | `SCALAR` | BSS | None |
| **cp** | `cp.c:92` | `pinfo` | Global | `sig_atomic_t` | `SCALAR` | BSS | None |
| **cp** | `cp.c:303` | `dnestack` | File-scope | `static int [MAXPATHLEN]` | `BUFFER` | BSS segment array | None |
| **cp** | `cp.c:304` | `dnesp` | File-scope | `static ssize_t` | `SCALAR` | BSS (Stack pointer index) | None (Leaked stack pointer corrupts nesting) |
| **cp (utils)** | `utils.c:101` (`copy_file`) | `buf` | Function-scope | `static char [MAXBSIZE]` | `BUFFER` | BSS segment array (storage valid across tasks) | None |
| **mv** | `mv.c:68` | `fflg, hflg, iflg, vflg` | File-scope | `static int` (4 vars) | `SCALAR` | BSS | None (Flags persist across runs) |
| **mv** | `mv.c:69` | `stdin_ok` | File-scope | `static int` | `SCALAR` | BSS | None |
| **mv** | `mv.c:70` | `pinfo` | File-scope | `static sig_atomic_t` | `SCALAR` | BSS | None |
| **mv** | `mv.c:276` (`fastcopy`) | `blen` | Function-scope | `static blksize_t` | `SCALAR` | BSS (Holds blocksize from 1st copy) | None (Causes `!blen` guard to fail on 2nd run) |
| **mv** | `mv.c:277` (`fastcopy`) | `bp` | Function-scope | `static char *` | `POINTER` | `malloc(sbp->st_blksize)` (freed by `task_release_allocations()` at task exit) | **YES (Use-After-Free on 2nd invocation when fastcopy runs)** |
| **rm** | `rm.c:63` | `dflag, eval, fflag, iflag, Pflag, stdin_ok, vflag, Wflag` | File-scope | `static int` (8 vars) | `SCALAR` | BSS | None (Flags & eval persist across runs) |
| **rm** | `rm.c:64` | `xflag` | File-scope | `static int` | `SCALAR` | BSS | None |
| **rm** | `rm.c:65` | `pinfo` | File-scope | `static sig_atomic_t` | `SCALAR` | BSS | None |
| **mkdir** | `mkdir.c` | *(none)* | — | — | — | No static or global variables | None |
| **echo** | `echo.c` | *(none)* | — | — | — | No static or global variables | None |
| **head** | `head.c` | *(none)* | — | — | — | No static or global variables (stack buffer) | None |
| **wc** | `wc.c:75` | `tlinect, twordct, tcharct, tlongest` | File-scope | `static wc_count_t` (4 vars) | `SCALAR` | BSS (Cumulative totals across files/runs) | None (Accumulator state leakage; reset by interim header) |
| **wc** | `wc.c:76` | `doline, doword, dobyte, dochar, dolongest` | File-scope | `static bool` (5 vars) | `SCALAR` | BSS (Option flags) | None (Flag state leakage; reset by interim header) |
| **wc** | `wc.c:77` | `rval` | File-scope | `static int` | `SCALAR` | BSS (Exit status) | None (Exit code leakage; reset by interim header) |
| **basename** | `basename.c` | *(none)* | — | — | — | No static or global variables | None |
| **basename (libc)** | `gen/basename.c:96` | `result` | Function-scope | `static char [PATH_MAX]` | `BUFFER` | BSS segment array (storage valid across tasks) | None |
| **dirname** | `dirname.c` | *(none)* | — | — | — | No static or global variables | None |
| **dirname (libc)** | `gen/dirname.c:97` | `result` | Function-scope | `static char [PATH_MAX]` | `BUFFER` | BSS segment array (storage valid across tasks) | None |
| **printenv** | `printenv.c` | *(none)* | — | — | — | No static or global variables (`environ` is extern) | None |
| **yes** | `yes.c` | *(none)* | — | — | — | No static or global variables | None |
| **ls** | `ls.c:75` | `printfcn` | File-scope | `static void (*)(DISPLAY *)` | `POINTER` | Code/text segment function pointer (e.g. `&printcol`) | None (Points to immutable code) |
| **ls** | `ls.c:76` | `sortfcn` | File-scope | `static int (*)(...)` | `POINTER` | Code/text segment function pointer (e.g. `&namecmp`) | None (Points to immutable code) |
| **ls** | `ls.c:82-85` | `blocksize, termwidth, sortkey, rval` | Global | `long`, `int` (4 vars) | `SCALAR` | BSS / Data segment globals | None (State/exit code leakage) |
| **ls** | `ls.c:88-115` | `f_accesstime`, `f_column`, `f_columnacross`, `f_flags`, `f_grouponly`, `f_humanize`, `f_commas`, `f_inode`, `f_listdir`, `f_listdot`, `f_longform`, `f_nonprint`, `f_nosort`, `f_numericonly`, `f_octal`, `f_octal_escape`, `f_recursive`, `f_reversesort`, `f_sectime`, `f_singlecol`, `f_size`, `f_statustime`, `f_stream`, `f_type`, `f_typedir`, `f_whiteout`, `f_fullpath`, `f_leafonly` | Global | `int` (28 flags) | `SCALAR` | BSS segment globals | None (Flag leakage across invocations) |
| **ls** | `ls.c:131` (`ls_main`) | `dot`, `dotav` | Function-scope | `static char []`, `static char *[]` | `STRING` / `BUFFER` | Data segment storage | None |
| **ls** | `ls.c:410` | `output` | File-scope | `static int` | `SCALAR` | BSS (Tracks if header was printed) | None (Formatting divergence on 2nd run) |
| **ls (print)** | `print.c:78` | `now` | File-scope | `static time_t` | `SCALAR` | BSS | None |
| **ls (print)** | `print.c:214` (`printcol`) | `array` | Function-scope | `static FTSENT **` | `POINTER` | `realloc(array, ...)` (freed by `task_release_allocations()` at task exit) | **YES (Use-After-Free / heap corruption on 2nd invocation)** |
| **ls (print)** | `print.c:215` (`printcol`) | `lastentries` | Function-scope | `static int` | `SCALAR` | Data segment initialized to `-1` | None (Causes `realloc` bypass on subsequent run) |
| **ls (print)** | `print.c:519-521` (`aclmode`) | `previous_dev`, `supports_acls`, `type` | Function-scope | `dev_t`, `int`, `int` | `SCALAR` | BSS / Data segment cache | None (Stale ACL device capability cache) |
| **ls (cmp)** | `cmp.c` | *(none)* | — | — | — | No static or global variables | None |
| **ls (util)** | `util.c` | *(none)* | — | — | — | No static or global variables | None |

---

## 4. Analysis by Utility

### A. Zero-Static Utilities (Clean Across Invocations)
The following utilities have **zero file-scope statics, zero function-scope statics, and zero globals**; they are entirely re-entrant and safe across multiple invocations in the same process:
1. `bin/mkdir/mkdir.c`
2. `bin/echo/echo.c`
3. `usr.bin/head/head.c` (stack buffer `char buf[65536]` inside 512KiB stack budget)
4. `usr.bin/basename/basename.c` (libc helper uses static `result[PATH_MAX]` `BUFFER`)
5. `usr.bin/dirname/dirname.c` (libc helper uses static `result[PATH_MAX]` `BUFFER`)
6. `usr.bin/printenv/printenv.c`
7. `usr.bin/yes/yes.c`
8. `bin/ls/cmp.c`
9. `bin/ls/util.c`

### B. Scalar / Buffer State Leakage Only (Incorrect Behavior, No UAF)
1. `bin/cp/cp.c`: 11 global flag ints, `myuid`, `myumask`, `pinfo`, `dnestack[MAXPATHLEN]`, `dnesp`. `dnesp` leakage breaks directory recursion on subsequent calls; flag leakage retains options across invocations.
2. `bin/rm/rm.c`: 8 option flags + `eval`, `xflag`, `pinfo`. `eval` leakage causes successful second `rm` to exit 1 if prior `rm` failed.
3. `usr.bin/wc/wc.c`: 4 count totals, 5 boolean flags, `rval`. Accumulates counts across separate session calls (currently reset by interim `cannedbsd_wc_state.h`).

### C. Heap Pointer Static Variables (Critical Use-After-Free Exposure)
1. **`bin/ls/print.c` (`printcol`)**:
   - `static FTSENT **array;` + `static int lastentries = -1;`
   - Dynamically reallocates `array` via `realloc()`. Freed at task exit by `task_release_allocations()`.
   - On next `ls`, if `entries <= lastentries`, `realloc` is skipped and freed pointers are written/read. If `entries > lastentries`, `realloc()` receives a freed pointer.
2. **`bin/mv/mv.c` (`fastcopy`)**:
   - `static char *bp;` + `static blksize_t blen;`
   - Allocated via `malloc(blen = sbp->st_blksize)` on first cross-device copy. Freed at task exit by `task_release_allocations()`.
   - On next `mv`, `!blen` evaluates to false, skipping `malloc`. `read()` and `write()` operate on freed memory `bp`.
3. **`bin/cat/cat.c` (`raw_cat`)**:
   - `static char *buf;` + `static size_t bsize;`
   - Allocated via `malloc(bsize)` when `bsize > sizeof(fb_buf)`. Freed at task exit by `task_release_allocations()`.
   - On next `cat`, `buf != NULL` evaluates to true, skipping `malloc`. `read()` operates on freed memory `buf`.

---

## 5. Scope Implications for `STATICS-RESET-01`

1. **Reset Scope Must Include Function-Scope Statics**:
   `STATICS-RESET-01` cannot restrict its scope to file-scope variables or external symbols. The most dangerous UAF defects in `ls` (`print.c`), `mv` (`mv.c`), and `cat` (`cat.c`) reside in **function-scope statics** (`static char *bp`, `static char *buf`, `static FTSENT **array`).
2. **Reset Scope Must Include Global Variables**:
   Utilities like `cp.c` and `ls.c` define flags as non-static globals rather than static variables (`int f_longform`, `int rflag`, `int Hflag`, etc.). A comprehensive reset mechanism must cover both static and global data segments for each pinned TU.
3. **Three Distinct UAF Sites Identified**:
   `ls` is **not** the only utility requiring UAF mitigation. Any generalized mechanism designed by `STATICS-RESET-01` must reset `array`/`lastentries` in `print.c`, `bp`/`blen` in `mv.c`, and `buf`/`bsize` in `cat.c`.
