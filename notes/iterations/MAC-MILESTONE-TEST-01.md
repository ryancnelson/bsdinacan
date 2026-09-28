# MAC-MILESTONE-TEST-01

## Base Evidence & Scope
- **Base Commit:** `b98b708` (origin/main)
- **Goal:** Derive exact small `create`, `list`, `copy`, `move`, `inspect`, and `delete` fixtures using the imported `cannedBSD` tools to prove the Mac guest milestone.
- **Constraints:** Documentation only. **PROPOSED CASES ARE STRICTLY UNEXECUTED.** Implementation is blocked on command-state fixes (e.g., `STATICS-CACHE-02` owned by Claude). No runtime, test code, or rig changes.

## Acceptance Cases & Mappings

These cases map directly to the `CB_MAC_CASE(command, expected_output, expected_status)` macro format in `platform/mac68k/acceptance_cases.def`. They are intentionally small to strictly respect guest transcript limits (minimizing VNC/UI automation overhead and avoiding large context limits).

### 1. Create & Inspect (`echo` and `cat`)
- **Fixture:** `echo test_data > /tmp/mac_test; cat /tmp/mac_test`
- **Expected Bytes:** `test_data\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo test_data > /tmp/mac_test; cat /tmp/mac_test", "test_data\n", 0)`

### 2. List (`ls`)
- **Fixture:** `ls /tmp/mac_test`
- **Expected Bytes:** `/tmp/mac_test\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("ls /tmp/mac_test", "/tmp/mac_test\n", 0)`

### 3. Copy & Inspect (`cp` and `cat`)
- **Fixture:** `cp /tmp/mac_test /tmp/mac_copy; cat /tmp/mac_copy`
- **Expected Bytes:** `test_data\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("cp /tmp/mac_test /tmp/mac_copy; cat /tmp/mac_copy", "test_data\n", 0)`

### 4. Move & Inspect (`mv` and `cat`)
- **Fixture:** `mv /tmp/mac_copy /tmp/mac_move; cat /tmp/mac_move`
- **Expected Bytes:** `test_data\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mv /tmp/mac_copy /tmp/mac_move; cat /tmp/mac_move", "test_data\n", 0)`

### 5. Negative Control - Missing Copy (`cat`)
- **Fixture:** `cat /tmp/mac_copy`
- **Expected Bytes:** `cat: /tmp/mac_copy: no such file or directory\n`
- **Expected Status:** `1`
- **Mapping:** `CB_MAC_CASE("cat /tmp/mac_copy", "cat: /tmp/mac_copy: no such file or directory\n", 1)`
- **Observation:** This acts as a negative control, proving that `mv` successfully unlinked the source file path.

### 6. Delete & Cleanup Observation (`rm` and `ls`)
- **Fixture:** `rm /tmp/mac_test /tmp/mac_move; ls /tmp/mac_move`
- **Expected Bytes:** `ls: /tmp/mac_move: no such file or directory\n`
- **Expected Status:** `1`
- **Mapping:** `CB_MAC_CASE("rm /tmp/mac_test /tmp/mac_move; ls /tmp/mac_move", "ls: /tmp/mac_move: no such file or directory\n", 1)`
- **Observation:** This proves both files were successfully unlinked and enforces deterministic cleanup before subsequent test automation iterations.

## Source-Grounded Uncertainties
- **Exact Error String Capitalization:** The expected bytes assume the lowercase `"no such file or directory\n"` from the libc `strerror(ENOENT)` table observed in `tests/acceptance_cases.def` (e.g. `libcerrprobe`). If the upstream NetBSD `ls` or `cat` binaries manually upper-case their diagnostic messages, the exact expected bytes may require adjustment upon actual execution.
- **`ls` Output Formatting:** The tests assume `ls` prints the explicit file path string when directly specified (`/tmp/mac_test\n`). The absence of multi-column or trailing formatting characters must be validated once command-state fixes are resolved.
