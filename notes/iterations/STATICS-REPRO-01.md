# STATICS-REPRO-01: Demonstrated Failure and Reachability Evidence

## Executive Summary

This iteration transforms the 5 findings from `STATICS-AUDIT-01` from source-code analysis into **demonstrated, executable evidence** under the `build/sanitize/bsdinacan` ASan/UBSan build.

Each test executes in a **single multi-command session** (`./build/sanitize/bsdinacan -c '...'`) to observe state leakage and heap lifecycle issues across consecutive task invocations in a shared data segment.

---

## Matrix of Results

| # | Utility | Finding / Vulnerability | Invocation Scenario | Observed Behavior | Status |
|---|---|---|---|---|---|
| 1 | **cat** | Heap Use-After-Free in `raw_cat()` (`buf`) | `cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2` | `AddressSanitizer: heap-use-after-free` on 2nd `cat` | **REPRODUCED (Active UAF)** |
| 2 | **mv** | Heap Use-After-Free in `fastcopy()` (`bp`) | `mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc` | Intra-mount `rename()` returns 0; `fastcopy()` uncalled | **LATENT (Landmine)** |
| 3 | **ls** | Heap Use-After-Free in `printcol()` (`array`) on `work/LS-02` | `ls /tmp` (3 files); `rm /tmp/f3`; `ls /tmp` (2 files) | `AddressSanitizer: heap-use-after-free` on 2nd `ls` | **REPRODUCED (Active UAF)** |
| 4 | **cp** | 7 persistent flags (`fflag`, `lflag`, `pflag`, `iflag`, `rflag`, `vflag`, `Nflag`) & stack pointer leakage (`dnesp`) | `cp -l /tmp/orig /tmp/lnk; cp /tmp/src /tmp/dst` / `cp -v ...; cp ...` | 2nd `cp` (no `-l`) attempts hardlink (`link()`); 2nd `cp` (no `-v`) outputs verbose progress | **REPRODUCED (Semantic & State Leakage)** |
| 5 | **rm** | Stale exit status `eval` leakage | `rm /tmp/nonexistent; rm /tmp/valid; echo $?` | 2nd successful `rm` exits with code `1` | **REPRODUCED (Exit Code Leakage)** |

---

## Detailed Test Evidence & Reachability Analysis

### 1. `cat` — Heap Use-After-Free (`cat.c:284` in `raw_cat()`)

- **Root Cause:**
  `cat.c:273` declares `static char *buf;`.
  In `raw_cat()`:
  ```c
  if (buf == NULL) {
      if (bsize <= sizeof(fb_buf))
          buf = fb_buf;
      else if ((buf = malloc(bsize)) == NULL) ...
  }
  ```
- **Reachability Mechanism:**
  `cb_libc.c:179` hardcodes `st_blksize = 1024` and `libc/include/stdio.h` defines `BUFSIZ` as 1024. Therefore, standard invocations evaluate `bsize <= sizeof(fb_buf)` (1024 <= 1024 = true) and point `buf` at the static array `fb_buf`, which stays valid across all invocations.
  However, when an explicit block size is provided via `cat -B <size>` with `size > 1024` (e.g. `cat -B 2048`), `buf` is allocated via `malloc(bsize)`.
  At task completion, `task_release_allocations()` frees all heap memory associated with the finished task.
  When a second `cat` runs in the same session, `buf` remains non-NULL, pointing to the freed heap block. `read()` writes directly into the freed heap buffer, triggering an active heap use-after-free.
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo hello > /tmp/cat1; echo world > /tmp/cat2; cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2'
  ```
- **Observed ASan Output:**
  ```text
  hello
  =================================================================
  ==ERROR: AddressSanitizer: heap-use-after-free on address 0xfdcfa5a60080 at pc 0xaaaae61a7e24 bp 0xff0fa5aac540 sp 0xff0fa5aabd30
  WRITE of size 6 at 0xfdcfa5a60080 thread T0
      #0 0xaaaae61a7e20 in read (/workspace/build/sanitize/bsdinacan+0xf7e20)
      #1 0xaaaae6216ecc in raw_cat (/workspace/build/sanitize/bsdinacan+0x166ecc)
  0xfdcfa5a60080 is located 0 bytes inside of 2048-byte region
  freed by thread T0 here: task_release_allocations() at task exit
  previously allocated by thread T0 here: malloc() in raw_cat()
  SUMMARY: AddressSanitizer: heap-use-after-free
  ```

---

### 2. `mv` — Cross-Mount `fastcopy()` Heap UAF (`mv.c:277` in `fastcopy()`)

- **Root Cause:**
  `mv.c:266` declares `static char *bp;`.
  When a cross-device move occurs (`rename()` returns `EXDEV`), `mv` falls back to `fastcopy()` (`mv.c:264`), which allocates `bp` via `malloc(blen)` on its first call:
  ```c
  if (bp == NULL && (bp = malloc(blen)) == NULL) ...
  ```
  On task exit, `task_release_allocations()` frees `bp`. A subsequent cross-device `mv` finds `bp != NULL` and reads/writes into freed memory.
- **Reachability Analysis:**
  In `cannedbsd`, all userland directory hierarchies (`/`, `/tmp`, `/home`, `/bin`) reside on the **single unified root `ramfs` mount** (`kernel->root_mount`).
  `cb_vfs_rename_path` returns `-CB_EXDEV` only when `old_node->mount != new_parent->mount`.
  Because secondary mounts are currently only constructed in C unit tests (`tests/test_core.c`) and no userland `mount` command exists in the shell session, all shell `mv` operations execute intra-mount `rename()`, which succeeds immediately with exit status 0.
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo test > /tmp/mva; mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc; cat /tmp/mvc'
  ```
- **Observed Output:**
  ```text
  test
  (exit status 0)
  ```
- **Status & Risk Assessment:**
  **LATENT LANDMINE (Fuse Unlit).** While unreachable in the current single-mount session environment, this defect will immediately trigger active heap corruption the moment secondary mounts (e.g. additional disk images or virtual filesystems) are exposed to userland commands.

---

### 3. `ls` — Default Column Mode Heap UAF (`print.c:214` on `work/LS-02`)

- **Root Cause:**
  `print.c:204` declares `static const DISPLAY *array;` and `static size_t lastentries;`.
  In `printcol()`:
  ```c
  if (dp->entries > lastentries) {
      if ((array = realloc(array, dp->entries * sizeof(DISPLAY *))) == NULL) ...
      lastentries = dp->entries;
  }
  ```
- **Reachability Mechanism:**
  When `ls` runs against a directory with $N$ entries (e.g., 3 files), `realloc()` allocates `array` for 3 entries and sets `lastentries = 3`.
  At task completion, `array` is freed by `task_release_allocations()`, but the global pointer `array` and `lastentries = 3` persist in the data segment.
  When a second `ls` runs on a directory with $\le 3$ entries (e.g. 2 files), `dp->entries > lastentries` (2 > 3) evaluates to **false**, bypassing `realloc()`. `printcol()` writes file entry pointers directly into the dangling heap memory, triggering an active heap use-after-free on every default multi-column console listing.
- **Reproduction Command (on `work/LS-02`):**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo 1 > /tmp/a1; echo 1 > /tmp/a2; echo 1 > /tmp/a3; ls /tmp; rm /tmp/a3; ls /tmp'
  ```
- **Observed ASan Output:**
  ```text
  a1  a2  a3
  =================================================================
  ==ERROR: AddressSanitizer: heap-use-after-free on address 0xfc3f9f600210
  WRITE of size 8 at 0xfc3f9f600210 thread T0
      #0 in printcol (/workspace/build/sanitize/bsdinacan+0x192f90)
  0xfc3f9f600210 is located 0 bytes inside of 40-byte region
  freed by thread T0 here: task_release_allocations() at task exit
  previously allocated by thread T0 here: realloc() in printcol()
  SUMMARY: AddressSanitizer: heap-use-after-free
  ```

---

### 4. `cp` — Global Flag Persistence (7 Unreset Flags) & Stack Leakage (`cp.c:90, 116, 304`)

- **Root Cause:**
  `cp.c:90` declares 11 global option flags:
  ```c
  int Hflag, Lflag, Rflag, Pflag, fflag, iflag, lflag, pflag, rflag, vflag, Nflag;
  ```
  In `cp.c:116` (`main()`), only 4 flags are reset:
  ```c
  Hflag = Lflag = Pflag = Rflag = 0;
  ```
  **SEVEN flags persist indefinitely across session invocations:** `fflag`, `iflag`, `lflag`, `pflag`, `rflag`, `vflag`, and `Nflag`.

- **Semantic & Security Hazards:**
  1. **`-f` (Silent Force-Overwrite):**
     If a user runs `cp -f`, `fflag` is set to 1. In subsequent plain `cp` invocations (without `-f`), if opening an existing destination file fails (e.g. read-only permissions), `utils.c:163` checks `if (to_fd == -1 && (fflag || tolnk))`. Because `fflag == 1` leaked, `cp` silently executes `unlink(to.p_path)` and overwrites the destination file without prompting or reporting an error.
  2. **`-l` (Silent Hardlink Creation vs. Data Copy):**
     If a user runs `cp -l`, `lflag` is set to 1. In subsequent plain `cp` invocations without `-l`, `utils.c:108` executes `if (lflag) { unlink(to.p_path); link(...); return 0; }`. Instead of copying file data, it creates hardlinks to the source file. (In `cannedbsd` ramfs where `link()` returns `ENOSYS`, the second `cp` fails with `cp: <dst>: function not implemented`).
  3. **`-p` (Attribute Preservation Persistence):**
     Causes all subsequent copies to attempt preserving timestamps, ownership, and permissions.
  4. **`-v` (Visible Progress Output Leakage):**
     Produces spurious stdout progress lines (`/src -> /dst`) for all subsequent `cp` commands.
  5. **`dnesp` Stack Pointer Leakage (`cp.c:304`):**
     `static char **dnesp;` retains a pointer to the caller's stack frame across invocations.

- **Reproduction Commands & Observed Output:**
  - **`lflag` Leakage:**
    ```sh
    ./build/sanitize/bsdinacan -c 'echo original > /tmp/cporig; echo target > /tmp/cptarget; cp -l /tmp/cporig /tmp/cplink; cp /tmp/cptarget /tmp/cpcpy; echo $?'
    ```
    ```text
    cp: /tmp/cplink: function not implemented
    cp: /tmp/cpcpy: function not implemented
    1
    ```
    *(Demonstrates that the second `cp`, invoked without `-l`, entered the `lflag` hardlink branch instead of performing a standard file copy).*

  - **`vflag` Leakage:**
    ```sh
    ./build/sanitize/bsdinacan -c 'echo 1 > /tmp/cpa; echo 2 > /tmp/cpb; cp -v /tmp/cpa /tmp/cpa_out; cp /tmp/cpb /tmp/cpb_out'
    ```
    ```text
    /tmp/cpa -> /tmp/cpa_out
    /tmp/cpb -> /tmp/cpb_out
    ```

---

### 5. `rm` — Stale `eval` Exit Code Leakage (`rm.c:63, 154`)

- **Root Cause:**
  `rm.c:63` declares `static int eval;` initialized to 0 at program load.
  `rm.c`'s `main()` never resets `eval = 0`.
  When any error occurs in `rm` (e.g. non-existent file), `eval` is set to 1 (`rm.c:154`).
  On all exits, `rm` calls `exit(eval)`.
- **Reachability Mechanism:**
  If an `rm` command fails earlier in a session, `eval` remains `1`. A subsequent `rm` command that succeeds completely will unlink its target successfully, but still exit via `exit(eval)` with exit status `1`, causing shell scripts and pipelines to falsely report command failure.
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'rm /tmp/nonexistent; echo rm1_status=$?; echo data > /tmp/valid; rm /tmp/valid; echo rm2_status=$?'
  ```
- **Observed Output:**
  ```text
  rm: /tmp/nonexistent: no such file or directory
  rm1_status=1
  rm2_status=1
  ```
