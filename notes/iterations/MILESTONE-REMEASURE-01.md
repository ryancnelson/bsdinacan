# MILESTONE-REMEASURE-01: File Manipulation Milestone Re-Measurement at `bae004b`

- **Status:** Complete; measurement recorded.
- **Base SHA:** `bae004b` (`origin/main` after `WC-02` merge).
- **Branch:** `work/MILESTONE-REMEASURE-01`.
- **Date:** 2026-09-17.
- **Environment:** Alpine Linux 3.22 container gate environment (`x86_64` / `aarch64`).
- **Scope:** Exploratory session drive of `build/bsdinacan` across all six milestone verbs (`create`, `list`, `copy`, `move`, `delete`, `inspect`) and targeted test areas (`cat -n/-b`, `wc` multi-file/multi-invocation statics isolation, `mkdir -p` / `rm -r` look-ahead cursor, `echo` vs `netbsdecho` registration, `ls` option rejection, missing path diagnostics, and pipeline composition). Zero ABI, runtime, or utility source changes.

---

## 1. Executive Summary

This re-measurement updates the milestone status from `89ef526` to `bae004b`, incorporating seven landed merges: `CP-01`, `FS-STAT-01`, `FORMAT-01`, `MKDIR-CMD-01`, `BUILD-SYNC-01`, `WC-02`, and `MILESTONE-E2E-01`.

### Key Findings:
1. **All Six Milestone Verbs Operational End-to-End:**
   - **Create:** `echo ... > /file`, `echo ... >> /file`, `mkdir -p /path/to/dir`.
   - **List:** `ls` (single-column directory listing; options rejected honestly per `LS-02` hold).
   - **Copy:** `cp` (pinned NetBSD `cp`, supporting file copies, file overwrites, and recursive tree replication `cp -r`).
   - **Move:** `mv` (pinned NetBSD `mv`, supporting file renames and directory moves).
   - **Delete:** `rm` (pinned NetBSD `rm`, supporting multi-file unlinks and recursive tree removal `rm -r`).
   - **Inspect:** `cat` (pinned NetBSD `cat` with `-n`, `-b`, `-s`, `-u`), `head` (pinned NetBSD `head` with `-n`), and `wc` (pinned NetBSD `wc` with `-l`, `-w`, `-c`, `-m`, `-L`, multi-file totals).

2. **Zero Static Pollution Across Invocations:**
   Executing `wc file1 file2` repeatedly in the same cooperative guest session produces identical per-file and total line/word/byte counts without accumulating totals across calls, validating the translation-unit-scoped `cannedbsd_wc_state.h` reset hook.

3. **VFS-05 Look-Ahead Cursor Exercised End-to-End:**
   `mkdir -p` on a 4-level deep directory tree (`/tmp/nest/l1/l2/l3`) populated with leaf and root files was completely removed by `rm -r /tmp/nest` in one session. Subsequent `ls /tmp` confirms complete directory deletion with zero orphan entries.

4. **Honest Failure Invariant Holds Across All Utilities:**
   Non-existent paths and invalid flags produce stderr diagnostics and exit status `1` (or `2` for usage errors) across all utilities without crashes or silent passes.

5. **Open Registration Convention Identified (`echo` vs `netbsdecho`):**
   The shell resolves `echo` to the bootstrap `echo_program` (`src/programs.c:44-64`), while the pinned NetBSD import is registered as `netbsdecho`. While basic arguments and `-n` flags produce identical output, this is the only utility in the repository where the pinned import was registered under an aliased name rather than replacing the bootstrap command directly at `/bin/echo`.

6. **Remaining Milestone Gap:**
   `list` (`ls`) remains the sole verb served by an owned placeholder without option support (`LS-02`), held behind libby's `STATICS-RESET-01`.

---

## 2. Detailed Session Measurements & Observed Outputs

### Session 1: `cat -n` and `cat -b` Line Numbering
- **Input Script:**
  ```sh
  echo "first line" > /tmp/f.txt; echo "" >> /tmp/f.txt; echo "third line" >> /tmp/f.txt; echo "" >> /tmp/f.txt; echo "fifth line" >> /tmp/f.txt
  cat -n /tmp/f.txt
  cat -b /tmp/f.txt
  cat -s /tmp/f.txt
  cat -ns /tmp/f.txt
  ```
- **Observed Output & Status:**
  - `cat -n`: Numbers all lines including blank lines (`1` to `5`).
    ```text
         1	first line
         2	
         3	third line
         4	
         5	fifth line
    ```
  - `cat -b`: Numbers only non-blank lines (`1` to `3`).
    ```text
         1	first line

         2	third line

         3	fifth line
    ```
  - `cat -s`: Squeezes consecutive blank lines.
  - `cat -ns`: Combines blank-line squeezing with line numbering.
  - **Exit Status:** `0`.
  - **Assessment:** Works completely; POSIX/BSD line numbering formatting verified.

---

### Session 2: `wc` Multi-File Totals & Statics Isolation
- **Input Script:**
  ```sh
  echo "line one alpha" > /tmp/w1; echo "line two beta" >> /tmp/w1; echo "line three gamma" >> /tmp/w1
  echo "first delta" > /tmp/w2; echo "second epsilon" >> /tmp/w2
  wc /tmp/w1 /tmp/w2
  wc /tmp/w1 /tmp/w2   # Invocation 2: testing static counter reset
  wc -l /tmp/w1 /tmp/w2
  wc -l /tmp/w1 /tmp/w2 # Invocation 2
  wc -w /tmp/w1 /tmp/w2
  wc -w /tmp/w1 /tmp/w2 # Invocation 2
  wc -c /tmp/w1 /tmp/w2
  wc -c /tmp/w1 /tmp/w2 # Invocation 2
  wc -L /tmp/w1 /tmp/w2
  wc -L /tmp/w1 /tmp/w2 # Invocation 2
  ```
- **Observed Output & Status:**
  - **`wc /tmp/w1 /tmp/w2` (Invocation 1):**
    ```text
           3       9      46 /tmp/w1
           2       4      27 /tmp/w2
           5      13      73 total
    ```
  - **`wc /tmp/w1 /tmp/w2` (Invocation 2 in same session):**
    ```text
           3       9      46 /tmp/w1
           2       4      27 /tmp/w2
           5      13      73 total
    ```
  - **`wc -l` (Invocation 1 & 2):** `3`, `2`, `5 total`.
  - **`wc -w` (Invocation 1 & 2):** `9`, `4`, `13 total`.
  - **`wc -c` (Invocation 1 & 2):** `46`, `27`, `73 total`.
  - **`wc -L` (Invocation 1 & 2):** `16`, `14`, `16 total`.
  - **Exit Status:** `0`.
  - **Assessment:** Verified zero state leakage across sequential invocations in a single guest session.

---

### Session 3: Deep `mkdir -p`, Population, Recursive `rm -r`, `ls` Verification
- **Input Script:**
  ```sh
  mkdir -p /tmp/nest/l1/l2/l3
  echo "top level" > /tmp/nest/top.txt
  echo "deep data" > /tmp/nest/l1/l2/l3/leaf.txt
  ls /tmp/nest
  ls /tmp/nest/l1/l2/l3
  cat /tmp/nest/l1/l2/l3/leaf.txt
  rm -r /tmp/nest
  ls /tmp
  ls /tmp/nest
  ```
- **Observed Output & Status:**
  - `ls /tmp/nest` outputs `top.txt\nl1`.
  - `ls /tmp/nest/l1/l2/l3` outputs `leaf.txt`.
  - `cat /tmp/nest/l1/l2/l3/leaf.txt` outputs `deep data`.
  - After `rm -r /tmp/nest`, `ls /tmp` shows `/tmp/nest` is completely removed.
  - `ls /tmp/nest` outputs `ls: /tmp/nest: no such file or directory` with status `1`.
  - **Assessment:** `VFS-05` look-ahead cursor and `MKDIR-CMD-01` verified end-to-end.

---

### Session 4: `echo` vs `netbsdecho` Registration Check
- **Input Script:**
  ```sh
  echo hello world; echo -n hello; echo " post-n"
  netbsdecho hello world; netbsdecho -n hello; echo " post-n"
  echo -n -n foo
  netbsdecho -n -n foo
  ```
- **Observed Output & Status:**
  - Both commands output identical text:
    ```text
    hello world
    hello post-n
    hello world
    hello post-n
    -n foo
    -n foo
    ```
  - **Registration Structure:**
    `src/programs.c:159` registers `"echo"` -> `echo_main` (bootstrap).
    `src/programs.c:178` registers `"netbsdecho"` -> `cb_netbsdecho_main` (NetBSD import).
  - **Assessment:** Functionality identical for standard invocations, but represents an unresolved naming duality.

---

### Session 5: `ls` with Options (LS-02 Held State Check)
- **Input Script:**
  ```sh
  ls /tmp
  ls -l /tmp
  ls -a /tmp
  ls -F /tmp
  ```
- **Observed Output & Status:**
  - `ls /tmp`: outputs directory entries (status 0).
  - `ls -l /tmp`: `usage: ls [file]` (status 1).
  - `ls -a /tmp`: `usage: ls [file]` (status 1).
  - `ls -F /tmp`: `usage: ls [file]` (status 1).
  - **Assessment:** Fails honestly with usage diagnostic and non-zero exit status. Confirms `LS-02` remains the sole unclosed utility gap.

---

### Session 6: Non-Existent Path Diagnostics & Error Propagation
- **Observed Output & Status:**
  - `cat /tmp/none`: `cat: /tmp/none: no such file or directory` (status 1).
  - `wc /tmp/none`: `wc: /tmp/none: no such file or directory` (status 1).
  - `head /tmp/none`: `head: /tmp/none: no such file or directory` (status 1).
  - `cp /tmp/none /tmp/dest`: `cp: /tmp/none: no such file or directory` (status 1).
  - `mv /tmp/none /tmp/dest`: `mv: rename /tmp/none to /tmp/dest: no such file or directory` (status 1).
  - `rm /tmp/none`: `rm: /tmp/none: no such file or directory` (status 1).
  - `mkdir /tmp/no_parent/child`: `mkdir: /tmp/no_parent/child: no such file or directory` (status 1).
  - `mkdir -p /tmp/no_parent/child`: creates directory structure cleanly (status 0).
  - **Assessment:** Complete honest error reporting across all commands.

---

### Session 7: Pipelines & Cross-Directory Workflow
- **Input Script:**
  ```sh
  mkdir -p /tmp/pipeline_test/in /tmp/pipeline_test/out
  echo "alpha bravo" > /tmp/pipeline_test/in/data.txt
  echo "charlie delta echo" >> /tmp/pipeline_test/in/data.txt
  cat /tmp/pipeline_test/in/data.txt | tr a-z A-Z
  cat /tmp/pipeline_test/in/data.txt | wc -w
  cat /tmp/pipeline_test/in/data.txt | wc -l
  cat /tmp/pipeline_test/in/data.txt | wc -c
  cp -r /tmp/pipeline_test/in /tmp/pipeline_test/backup
  mv /tmp/pipeline_test/backup /tmp/pipeline_test/archived
  rm -r /tmp/pipeline_test
  ls /tmp
  ```
- **Observed Output & Status:**
  - `tr` uppercase transformation: `ALPHA BRAVO\nCHARLIE DELTA ECHO`.
  - `wc -w`: `       5`.
  - `wc -l`: `       2`.
  - `wc -c`: `      31`.
  - Recursive directory tree replication (`cp -r`), directory rename (`mv`), and recursive deletion (`rm -r`) completed with exit status `0`.
  - **Assessment:** Full pipeline composition and complex cross-directory workflows operational end-to-end.

---

## 3. Milestone Verb Scorecard at `bae004b`

| Verb | Utility | Implementation Origin | Option Support | Session Status |
| :--- | :--- | :--- | :--- | :--- |
| **Create** | `echo`, `mkdir` | Pinned NetBSD `mkdir.c` / Bootstrap `echo` | `mkdir -p`, `echo -n` | **Operational** |
| **List** | `ls` | CannedBSD-owned placeholder (`commands/ls.c`) | None (`usage: ls [file]`) | **Operational (Plain only, LS-02 held)** |
| **Copy** | `cp` | Pinned NetBSD `cp.c` + `utils.c` | `cp -r`, `cp -p`, `cp -f` | **Operational** |
| **Move** | `mv` | Pinned NetBSD `mv.c` | `mv -f`, directory moves | **Operational** |
| **Delete** | `rm` | Pinned NetBSD `rm.c` | `rm -r`, `rm -f`, multi-file | **Operational** |
| **Inspect** | `cat`, `head`, `wc` | Pinned NetBSD `cat.c`, `head.c`, `wc.c` | `cat -n/-b/-s/-u`, `head -n`, `wc -l/-w/-c/-m/-L` | **Operational** |

---

## 4. Open Non-Blocker Items

1. **`LS-02` (Milestone Gating):** Unchanged NetBSD `ls` with `-l`, `-a`, `-F`, `-1`, and column formatting (held behind `STATICS-RESET-01`).
2. **`ECHO-02` (Naming Convention):** Unify `netbsdecho` onto `/bin/echo`, retiring bootstrap `echo_main` in `src/programs.c`.
