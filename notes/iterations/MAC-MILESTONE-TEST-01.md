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
- Total added bytes across all 10 proposed cases below: **313 bytes**.
- Final computed `result` length: 2025 bytes (total including NUL is 2026, leaving 22 bytes safely within the 2048-byte limit).

### 1. Create & Inspect (`echo` and `cat`)
- **Fixture:** `echo y>f;cat f` (Adds 20 bytes)
- **Expected Bytes:** `y\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cat f", "y\n", 0)`

### 2. List Directory Setup (`mkdir` and `ls`)
- **Fixture:** `mkdir d;echo y>d/f;ls d` (Adds 29 bytes)
- **Expected Bytes:** `f\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo y>d/f;ls d", "f\n", 0)`

### 3. Copy Command Status (`cp`)
- **Fixture:** `echo y>f;cp f c` (Adds 21 bytes)
- **Expected Bytes:** `""` (Empty string)
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cp f c", "", 0)`

### 4. Copy Inspect (`cat`)
- **Fixture:** `echo y>f;cp f c;cat c` (Adds 27 bytes)
- **Expected Bytes:** `y\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;cp f c;cat c", "y\n", 0)`

### 5. Move Command Status (`mv`)
- **Fixture:** `echo y>f;mv f m` (Adds 21 bytes)
- **Expected Bytes:** `""`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("echo y>f;mv f m", "", 0)`

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

### 8. Pre-Deletion Multi-File Directory Observation (`ls`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;ls d` (Adds 40 bytes)
- **Expected Bytes:** `b\na\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;ls d", "b\na\n", 0)`
- **Observation:** Verified via `tests/test_ls_behavior.sh` and `src/ramfs.c` (`node_create` prepends dynamically), RAMFS child lists return elements in reverse insertion order.
- **Notice:** Future `LS-02` changes sorting/ordering and will require revalidation of this output string.

### 9. Delete Command Status (`rm`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b` (Adds 46 bytes)
- **Expected Bytes:** `""`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;rm d/a d/b", "", 0)`

### 10. Bounded Lifecycle Delete Cleanup Observation (`ls`)
- **Fixture:** `mkdir d;echo 1>d/a;echo 2>d/b;ls d;rm d/a d/b;ls d` (Adds 56 bytes)
- **Expected Bytes:** `b\na\n`
- **Expected Status:** `0`
- **Mapping:** `CB_MAC_CASE("mkdir d;echo 1>d/a;echo 2>d/b;ls d;rm d/a d/b;ls d", "b\na\n", 0)`
- **Observation:** This rigidly binds the before and after states. A silent setup failure won't emit the `b\na\n` prefix, instantly mismatching.

## Intentional Mutation Rejections
To verify the fidelity of the test harness assertions, the explicitly crafted exact-output matchers double as mutation controls against silent system failures:
- **Mutation (No-op `rm`):** An incomplete `rm` implementation that leaves files intact safely fails in Case 10. If `rm` acts as a no-op, the final `ls d` would emit `b\na\n`, generating a total string of `b\na\nb\na\n` which decisively rejects the strictly expected `"b\na\n"`.
- **Mutation (`mv` without unlink):** An incomplete `mv` implementation that copies but fails to unlink the source safely fails in Case 7. If the source file remains, `ls f` evaluates to `f\n` with a status of `0`, instantly failing the expectation of exactly `ls: f: no such file or directory\n` and status `1`.
