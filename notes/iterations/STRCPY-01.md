## STRCPY-01 implementation

-   **Goal**: Pin unchanged NetBSD strcpy with private declaration/renaming `cb_libc_strcpy`, source hash/license and existing string import conventions, and ensure it passes behavioral and boundary controls.
-   **Implementation steps taken**:
    -   Fetched `strcpy.c` exactly matching the pinned NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
    -   Verified that the source contains an `#undef strcpy`.
    -   Added the compiler renaming adapter `__asm__("cb_libc_strcpy")` to `libc/include/string.h` guarded by `CANNEDBSD_BUILDING_LIBC_STRCPY`.
    -   Registered `strcpy.c` in `Makefile` identical to how `strcmp.c` is compiled (using `$(CFLAGS)` and identical boundary checks) and added its object to the `libcannedbsd.a` archive.
    -   Updated `tests/test_netbsd_libc_source.sh` to enforce `strcpy` provenance, hashes, and boundary strictness (`cb_libc_strcpy` exporting, no host `strcpy` leaking).
    -   Wrote a dedicated ordinary probe `tests/libc_strcpy_probe.c` mapping exactly to the acceptance tests criteria (destination identity check, terminating NUL canary check, empty string test, and unsigned binary byte tests) which natively asserts against our custom `strcpy` and does not taint the global testing boundary code like `wc.c`.
    -   Updated `tests/test_libc_source.sh` with a specific test block for the `strcpyprobe` object ensuring that it properly restricts itself to `cb_libc_strcpy`.
    -   Added Mac CMake target `strcpyprobe_command.o` and registered the command in `platform/mac68k/main.c`.
    -   Configured Mac Acceptance with the correct output cases `CB_MAC_CASE("strcpyprobe", "", 0)` and adjusted guest transcript assertion counter to 51 cases (`50 + 1`).

-   **Negative Control Testing (Biggie container)**:
    -   A temporary injection to `tests/libc_strcpy_probe.c` deliberately corrupting `strcpy` output confirmed that it honestly fails with a non-zero exit code locally without `__asm__` symbol linking breaking.
    -   The script `tests/test_libc_source.sh` accurately catches ordinary `wc.c` referencing standard `strcpy` indicating boundary protection is strong.
    -   Reverting those changes returned all natively evaluated test modes on `biggie` to passing cleanly:
        `build/test_core` -> `all core tests passed`
        `tests/test_netbsd_libc_source.sh` -> `pinned unmodified NetBSD libc source boundary passed`
        `tests/test_libc_source.sh` -> `external libc source boundary passed`
        `tests/test_architecture.sh` -> `architecture boundary checks passed`

-   **Mac Guest**:
    -   Python tests (`python3 -B tests/test_mac_guest.py`) ran 18 tests and passed locally.
    -   We defer full UI checking / system guest runs to the root review process.

-   **Status**: Ready for root review/integration.
