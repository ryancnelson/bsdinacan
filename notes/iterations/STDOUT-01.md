# STDOUT-01 Red/Green Implementation Record

This artifact records the gaps identified during the `dc1aeeb` patch review and the honest implementations that resolve them to a clean green state.

## Gaps Addressed & Resolved

1. **`initialize_api` accessor wiring**: 
   - *Red*: Dynamic setup missed appending the `stdio_state_location` accessor into the `cb_api_v1` copy inside `initialize_api`.
   - *Green*: Explicitly initialized `api->stdio_state_location = api_stdio_state_location;` in `src/core.c`.
2. **Version and Size/NULL State Validation**: 
   - *Red*: `check_stdio_state_available` lacked the `abi_version == CB_ABI_VERSION_V1` validation. `mark_stdio_error` was missing the `abi_version` enforcement too.
   - *Green*: Added full bounds checking ensuring both version, structural bounds, and function pointer presence exist before accessing or mutating task state.
3. **`write_all` errno behavior**: 
   - *Red*: Success was squashing pre-existing errno since lower APIs like RAMFS clear it. It also didn't explicitly preserve the `actual_error` yielded from the driver failure.
   - *Green*: The initial errno is saved dynamically upon entry to `write_all`. On full success, the saved errno is forcefully restored via `set_errno`. On failure, the localized host failure is saved explicitly and reset back after marking `EIO`/flags.
4. **CMake & Main Mac Registration**: 
   - *Red*: Module ignored in the Mac compilation config, causing undefined behavior.
   - *Green*: Added `cb_stdio_state_probe` to `platform/mac68k/CMakeLists.txt` and successfully populated `CannedBSD`'s target object payload. Added `cb_stdio_oldtable_program` alongside it inside `main.c`. 
5. **Exact Shared Count Reconciled**: 
   - *Red*: `tests/test_mac_guest.py` expected exactly 29 transcript tests, conflicting with the 32 acceptance lines.
   - *Green*: Elevated the `test_mac_guest.py` expected output validation lines to 33, representing the strictly enforced payload length accounting for exactly 4 appended `libcstdiostateprobe` cases natively.
6. **Isolated Private Probe**: 
   - *Red*: Probes previously cheated using CannedBSD's `#include "cannedbsd/libc.h"` causing pollution, and utilized internal dependencies like `cb_libc_strcmp`.
   - *Green*: Enforced `#define CANNEDBSD_SOURCE_FENCE` dropping all internal aliases. Only private `stdio.h`, `errno.h`, `string.h` dependencies are consumed.
7. **Comprehensive Failure/Interleaving Test Suites (Test Matrix)**: 
   - *Red*: Branches for `failedexecprobe` and the state independence mocks lay unlinked or dead. The previous `execprobe` falsely passed when the structural interface was fundamentally missing.
   - *Green*: Fully integrated into `test_core.c` via mock injection.
     - **Interleaving**: `stdio_interleave1` sets `stdout` error then `yield`s. `stdio_interleave2` sets `stderr` error then `yield`s. Upon wake, both strictly assert zero contamination.
     - **Failed/Successful Exec**: Validates structural presence explicitly (`api->stdio_state_location == NULL` triggers failure instead of skipping). Verifies reset states across success boundaries and intact states across `ENOENT`. 
     - **Partial Writes/Zero Writes**: Native intercept loops inject `mock_write_partial_countdown` yielding fractioned counts triggering next-cycle mock failure, and exact zero bytes triggering `CB_EIO`. 
     - **Old/NULL/Wrong-Version/Short-State**: Emulated sequentially in `test_stdio_oldtable_main`, deliberately corrupting `.struct_size`, `.abi_version`, `stdio_state_location` pointer, and finally exposing a short `cb_stdio_state_v1` return payload. Standard `ENOSYS` behavior properly intercepts and secures the payload.

All fixes have been pushed to `work/STDOUT-01`.

## Iteration 2 (a02fcb6 review) Fixes
- **Returned State validation**: Implemented `state->abi_version == CB_ABI_VERSION_V1` check securely across BOTH `check_stdio_state_available` and `mark_stdio_error`.
- **Shared Scoped Variants Wrapper**: Replaced the native `CB_LIBC_PROGRAM` `stdiooldtable` mapping with a standalone `stdiovariants_main`. This safely crafts and feeds mocked state variants (NULL, short size, wrong version) via `cb_libc_start` locally against `cb_stdio_oldtable_main`, guaranteeing inner state tests receive the mocked outer payload without aborting at the global launcher, and perfectly restores the `original_api` reference prior to teardown.
- **Fixture Scoping Constraint**: `FIXTURE_FULL` exceeded the hard 64 capacity limit. Successfully scoped the testing suite out to an isolated `FIXTURE_STDIO` capacity container.
- **Uncalled Tests Invocation**: Plumbed a direct, dedicated test execution loop `test_stdio_state()` into `int main()` ensuring all newly provisioned states, branches, and intercepts unconditionally execute.
