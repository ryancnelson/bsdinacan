# MILESTONE-E2E-01: Automated End-to-End File Manipulation Session Behavioral Test

- **Status:** Complete and verified; ready for review.
- **Base SHA:** `5b36ca2` (`origin/main` after MAC68K-CMD-01 merge).
- **Branch:** `work/MILESTONE-E2E-01`.
- **Hypothesis:** A single automated behavioral test suite (`tests/test_file_manipulation_session.sh`) can drive and verify the milestone acceptance sentence ("a shell session in which a user can create, list, copy, move, delete and inspect files using unchanged NetBSD utilities") across all six verbs, asserting exact output and exit status at each step while ensuring regressions in shell command integration or registered program dispatch fail loudly and detectably.

---

## 1. Red (First-Failure Diagnostic Measurement)

- **Measurement:** Temporarily unregistered `cb_cp_program` in `src/programs.c` to simulate a dropped utility or command dispatch regression, rebuilt `bsdinacan`, then ran `tests/test_file_manipulation_session.sh`.
- **Observed first failure:**
  ```text
  FAIL: full lifecycle session through all six file manipulation verbs: expected status 0, got 127
  ```

---

## 2. Green (Implementation & Verification)

- **Test Suite:** `tests/test_file_manipulation_session.sh` wired into `Makefile` `test:` target.
- **Test Scenarios:**
  1. **Full Lifecycle Session:** Drives a single continuous guest session executing the complete sequence of all six milestone verbs:
     - **Create:** `echo "first line of alpha" > /tmp/alpha; echo "second line of alpha" >> /tmp/alpha; echo "data content for beta" > /tmp/beta`
     - **List:** `ls /tmp` (verifying RAMFS prepend ordering: `beta`, `alpha`)
     - **Inspect:** `cat /tmp/alpha`, `head -n 1 /tmp/alpha`, `wc -c /tmp/beta`
     - **Copy:** `cp /tmp/alpha /tmp/alpha_bak`, `cp -r /home/user /tmp/user_copy` (file copy and recursive directory copy)
     - **Move:** `mv /tmp/alpha_bak /tmp/alpha_moved`
     - **Delete:** `rm /tmp/beta`, `rm -r /tmp/user_copy`, `rm /tmp/alpha /tmp/alpha_moved`
     - **Final Verification:** `ls /tmp` asserting empty directory after teardown.
  2. **Pipeline Composition:** Verifies pipeline integration (`ls /tmp | tr a-z A-Z`, `cat /tmp/item_copy | tr a-z A-Z`).
  3. **Cross-Directory Tree Manipulation:** Exercises multi-mount/multi-directory workflows between `/home/user` and `/tmp`.
  4. **Status Code Propagation & Non-Fatal Error Recovery:** Validates `$?` expansion across successive successes, missing-file diagnostics, and cleanup.
  5. **Interactive Stdin Session:** Drives the full 6-verb suite through standard input, asserting interactive `cannedBSD$ ` prompts and output.

---

## 3. Zero-Growth ABI Invariant

- **`include/cannedbsd/abi.h`**: **Strictly 0 changes.**
- Zero modification to public kernel ABI or libc veneer interfaces.
