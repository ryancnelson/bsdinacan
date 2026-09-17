# STATICS-REPRO-01: Demonstrated Failure and Reachability Evidence

## Executive Summary

This iteration transforms the 5 findings from `STATICS-AUDIT-01` from source-code analysis into **demonstrated, executable evidence** under the `build/sanitize/bsdinacan` ASan/UBSan build.

Each test executes in a **single multi-command session** (`./build/sanitize/bsdinacan -c '...'`) to observe state leakage and heap lifecycle issues across consecutive task invocations in a shared data segment.

---

## Matrix of Results

| # | Utility | Finding / Vulnerability | Invocation Scenario | Observed Behavior | Status |
|---|---|---|---|---|---|
| 1 | **cat** | Heap Use-After-Free in `raw_cat()` (`buf`) | `cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2` | `AddressSanitizer: heap-use-after-free` on 2nd `cat` | **REPRODUCED (Active UAF)** |
| 2 | **mv** | Heap Use-After-Free in `fastcopy()` (`bp`) | `mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc` | Intra-mount `rename()` returns 0; `fastcopy()` uncalled | **LATENT (Unreachable in session)** |
| 3 | **ls** | Heap Use-After-Free in `printcol()` (`array`) on `work/LS-02` | `ls /tmp` (3 files); `rm /tmp/f3`; `ls /tmp` (2 files) | `AddressSanitizer: heap-use-after-free` on 2nd `ls` | **REPRODUCED (Active UAF)** |
| 4 | **cp** | Option flag persistence & stack leakage (`vflag`, `dnesp`) | `cp -v /tmp/a /tmp/a_out; cp /tmp/b /tmp/b_out` | 2nd `cp` (no `-v`) outputs `/tmp/b -> /tmp/b_out` | **REPRODUCED (State Leakage)** |
| 5 | **rm** | Stale exit status `eval` leakage | `rm /tmp/nonexistent; rm /tmp/valid; echo $?` | 2nd successful `rm` exits with code `1` | **REPRODUCED (Exit Code Leakage)** |

---

## Detailed Test Evidence

### 1. `cat` — Heap Use-After-Free (`cat.c:284` in `raw_cat()`)
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo hello > /tmp/cat1; echo world > /tmp/cat2; cat -B 2048 /tmp/cat1; cat -B 2048 /tmp/cat2'
  ```
- **Observed ASan Output:**
  ```text
  hello
  =================================================================
  ==ERROR: AddressSanitizer: heap-use-after-free on address 0xfdcfab5c0080
  WRITE of size 6 at 0xfdcfab5c0080 thread T0
      #0 0xaaaae9b67e20 in read (/workspace/build/sanitize/bsdinacan+0xf7e20)
      #1 0xaaaae9bd6ecc in raw_cat (/workspace/build/sanitize/bsdinacan+0x166ecc)
  0xfdcfab5c0080 is located 0 bytes inside of 2048-byte region
  freed by thread T0 here: task_release_allocations() at task exit
  previously allocated by thread T0 here: malloc() in raw_cat()
  SUMMARY: AddressSanitizer: heap-use-after-free
  ```
- **Reachability Analysis:**
  Reachable whenever `-B <size>` is passed with `size > sizeof(fb_buf)` (1024). Default runs without `-B` use the static buffer `fb_buf`, but any explicit `-B > 1024` immediately arms the heap UAF for subsequent calls.

---

### 2. `mv` — Cross-Mount `fastcopy()` Heap UAF (`mv.c:277` in `fastcopy()`)
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo test > /tmp/mva; mv /tmp/mva /home/user/mvb; mv /home/user/mvb /tmp/mvc; cat /tmp/mvc'
  ```
- **Observed Output:**
  ```text
  test
  (exit status 0)
  ```
- **Reachability Analysis:**
  In `cannedbsd`, all userland paths (`/`, `/tmp`, `/home`, `/bin`) reside on the single unified root `ramfs` mount (`kernel->root_mount`). `cb_vfs_rename_path` only returns `-CB_EXDEV` when `old_node->mount != new_parent->mount`. Because secondary mounts are only constructed in C unit tests (`tests/test_core.c`) and no userland `mount` command exists in the shell session, `rename()` always succeeds with 0.
  **Conclusion:** `mv`'s `fastcopy()` UAF is **LATENT** and currently unreachable from userland shell sessions.

---

### 3. `ls` — Default Column Mode Heap UAF (`print.c:214` on `work/LS-02`)
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
- **Reachability Analysis:**
  Reachable in default column mode (console stdout) whenever a subsequent `ls` lists a directory with `entries <= lastentries` from a prior listing. `realloc` is bypassed, and pointers are written directly to freed heap memory.

---

### 4. `cp` — Global Flag & Stack Leakage (`cp.c:90, 304`)
- **Reproduction Command:**
  ```sh
  ./build/sanitize/bsdinacan -c 'echo 1 > /tmp/cpa; echo 2 > /tmp/cpb; cp -v /tmp/cpa /tmp/cpa_out; cp /tmp/cpb /tmp/cpb_out'
  ```
- **Observed Output:**
  ```text
  /tmp/cpa -> /tmp/cpa_out
  /tmp/cpb -> /tmp/cpb_out
  ```
- **Analysis:**
  In `cp.c`'s `main()`, only `Hflag = Lflag = Pflag = Rflag = 0;` are reinitialized. `vflag`, `pflag`, `lflag`, `fflag`, `iflag`, `rflag`, and `Nflag` remain set across invocations. The second `cp` command inherits `vflag = 1` and unexpectedly emits verbose output.

---

### 5. `rm` — Stale `eval` Exit Code Leakage (`rm.c:63`)
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
- **Analysis:**
  When the first `rm` fails, `eval` is set to `1`. `rm.c`'s `main()` never resets `eval = 0`. The second `rm` successfully removes `/tmp/valid`, but exits via `exit(eval)` with `eval == 1`. Subsequent successful `rm` invocations falsely report failure.
