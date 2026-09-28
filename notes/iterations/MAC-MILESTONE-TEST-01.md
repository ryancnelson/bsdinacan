# MAC-MILESTONE-TEST-01

## Base Evidence & Scope
- **Base Commit:** `b98b708ee94dd59362ba904daab6428942debdfb`
- **Goal:** Derive exact small `create`, `list`, `copy`, `move`, `inspect`, and `delete` fixtures using the imported `cannedBSD` tools to prove the Mac guest milestone.
- **Constraints:** Documentation only. **PROPOSED CASES ARE STRICTLY UNEXECUTED.** Implementation is blocked on command-state fixes (e.g., `STATICS-CACHE-02` owned by Claude). No runtime, test code, or rig changes.

## Acceptance Cases & Mappings

These cases map directly to the `CB_MAC_CASE(command, expected_output, expected_status)` macro format in `platform/mac68k/acceptance_cases.def`. 
`platform/mac68k/main.c` utilizes a fresh kernel for every `CB_MAC_CASE`. Consequently, cases cannot share state across runs; each fixture independently sets up its required state using minimal relative paths to conserve buffer limits.

### Budget Verification
`main.c` concatenates `"PASS <command>\n"` to `result[2048]` upon success. 
- Current `expected-result` length: 1712 bytes.
- Remaining budget: 335 bytes.
- Total added bytes across all 11 proposed cases below: **318 bytes**.
- Final computed `result` length: 2030 bytes, safely within the 2048-byte limit.
Output capture (the `expected_output` buffer) is managed entirely separately by `cb_mac_capture_matches` and is bounded appropriately.

### 1. Create & Inspect (`echo` and `cat`)
- **Fixture:** `echo y>f;cat f` (Adds 20 bytes)
- **Expected Bytes:** `y\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cat f", "y\n", 0)`

### 2. List Directory Setup (`mkdir` and `ls`)
- **Fixture:** `mkdir d;echo y>d/f;ls d` (Adds 30 bytes)
- **Expected Bytes:** `f\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo y>d/f;ls d", "f\n", 0)`
- **Derivation:** `commands/ls.c` is an owned component (not imported NetBSD `ls`) that iterates over `readdir` and calls `puts(entry->d_name)`. Thus, it prints exactly `f\n` without columns or metadata.

### 3. Copy Command Status (`cp`)
- **Fixture:** `echo y>f;cp f c` (Adds 20 bytes)
- **Expected Bytes:** `""` (Empty string)
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cp f c", "", 0)`
- **Observation:** This asserts the exact intermediate command status of `cp` by positioning it as the final command.

### 4. Copy Inspect (`cat`)
- **Fixture:** `echo y>f;cp f c;cat c` (Adds 27 bytes)
- **Expected Bytes:** `y\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cp f c;cat c", "y\n", 0)`

### 5. Move Command Status (`mv`)
- **Fixture:** `echo y>f;mv f m` (Adds 20 bytes)
- **Expected Bytes:** `""`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;mv f m", "", 0)`
- **Observation:** Asserts the exact command status of `mv`.

### 6. Move Inspect (`cat`)
- **Fixture:** `echo y>f;mv f m;cat m` (Adds 27 bytes)
- **Expected Bytes:** `y\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;mv f m;cat m", "y\n", 0)`

### 7. Move Unlinked Source Verification (`ls`)
- **Fixture:** `echo y>f;mv f m;ls f` (Adds 26 bytes)
- **Expected Bytes:** `ls: f: no such file or directory\n`
- **Expected Status:** `1`
- **Mapping:** `CB_MAC_CASE("echo y>f;mv f m;ls f", "ls: f: no such file or directory\n", 1)`
- **Observation:** Proves `mv` successfully unlinked the source path.

### 8. Pre-Deletion Multi-File Directory Observation (`ls`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;ls d` (Adds 40 bytes)
- **Expected Bytes:** `a\nb\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;ls d", "a\nb\n", 0)`
- **Observation:** Proves that an actual directory exists containing precisely both targeted files before attempting deletion.

### 9. Delete Command Status (`rm`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b` (Adds 46 bytes)
- **Expected Bytes:** `""`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b", "", 0)`
- **Observation:** Asserts the `rm` status itself without allowing a subsequent successful command to conceal a failure.

### 10. Delete Cleanup Observation (`ls`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b;ls d` (Adds 51 bytes)
- **Expected Bytes:** `""`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b;ls d", "", 0)`
- **Observation:** Verifies the directory is completely empty, conclusively proving both files were removed.

### 11. Negative Control: Intentional Failure Rejection (`cat`)
- **Fixture:** `cat Z` (Adds 11 bytes)
- **Expected Bytes:** `cat: Z: no such file or directory\n`
- **Expected Status:** `1`
- **Mapping:** `CB_MAC_CASE("cat Z", "cat: Z: no such file or directory\n", 1)`
- **Observation:** Represents an explicitly broken operation as a test-harness negative control, verifying that missing paths generate exact rejections.
