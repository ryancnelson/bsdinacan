## STRCPY-01 implementation

-   **Goal**: Pin unchanged NetBSD strcpy with private declaration/renaming `cb_libc_strcpy`, source hash/license and existing string import conventions, and ensure it passes behavioral and boundary controls.
-   **Implementation steps taken**:
    -   Fetched `strcpy.c` exactly matching the pinned NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
    -   Verified that the source contains an `#undef strcpy`.
    -   Added the compiler renaming adapter `__asm__("cb_libc_strcpy")` to `libc/include/string.h` guarded by `CANNEDBSD_BUILDING_LIBC_STRCPY`.
    -   Registered `strcpy.c` in `Makefile` identical to how `strcmp.c` is compiled (using `$(CFLAGS)` and identical boundary checks) and added its object to the `libcannedbsd.a` archive.
    -   Updated `tests/test_netbsd_libc_source.sh` to enforce `strcpy` provenance, hashes, and boundary strictness (`cb_libc_strcpy` exporting, no host `strcpy` leaking).
    -   Wrote a dedicated ordinary probe `tests/libc_strcpy_probe.c` mapping exactly to the acceptance tests criteria (destination identity check, terminating NUL canary check, empty string test, and unsigned binary byte tests) which natively asserts against our custom `strcpy` and does not taint the global testing boundary code like `wc.c`.
    -   Updated `tests/test_libc_source.sh` with a specific test block for the `strcpyprobe` object ensuring that it properly restricts itself to `cb_libc_strcpy` without modifying existing unrelated checks.
    -   Fixed `platform/mac68k/CMakeLists.txt` to correctly include both `strcpy.c` and `libc_strcpy_probe_module.c` as library/probe objects into `CannedBSD`.
    -   Configured Mac Acceptance with the correct output cases `CB_MAC_CASE("strcpyprobe", "", 0)` and adjusted guest transcript assertion counter to 51 cases (`50 + 1`).

-   **Negative Control Testing (Biggie container)**:
    -   Exported a clean tree to `/tmp/export`, then ran `sed -i '' -e 's/return(save);/return(save+1);/' /tmp/export/upstream/netbsd/common/lib/libc/string/strcpy.c` to deliberately corrupt the implementation's return value.
    -   Executed the test on Biggie via Docker: `tar -czf - -C /tmp/export . | ssh ryan@biggie 'cd /tmp/bsdinacan-strcpy-neg && rm -rf * && tar -xz && docker run --rm -v $(pwd):/work -w /work bash:latest bash -c "apk add --no-cache make gcc musl-dev libucontext-dev clang compiler-rt python3 ripgrep bsd-compat-headers file >/dev/null && make clean && BUILD_PATH=build make LDLIBS=-lucontext build/test_core && ./build/test_core"'`
    -   The unchanged probe correctly caught the failure, aborting with: `command: strcpyprobe` / `expected status/output: 0 <>` / `actual status/output: 1 <>`

-   **CI Results (Actual execution)**:
    -   Running tests on the clean upstream source cleanly passes all execution on `biggie` (our manual CI analog):
        `build/test_core` -> `all core tests passed`
        `tests/test_netbsd_libc_source.sh` -> `pinned unmodified NetBSD libc source boundary passed`
        `tests/test_libc_source.sh` -> `external libc source boundary passed`
        `tests/test_architecture.sh` -> `architecture boundary checks passed`

-   **Mac Guest**:
    -   Python tests (`python3 -B tests/test_mac_guest.py`) ran 18 tests and passed locally in 0.346s.
    -   We defer full UI checking / system guest runs to the root review process.

-   **Status**: Ready for root review/integration.

## Coordinator acceptance

Worker correction `62802d0b16422a7396697a062a98cf3a8f6ae193` was independently
reviewed with corrected Mac build linkage. Its unchanged probe was independently
run against the retained disposable `return(save+1)` mutation and failed with
status 1; this is after-implementation regression-control evidence.

Integration `bb1190ce0d911bb333d788df58ed7df6538b5e46` passed all three exact
Woodpecker #280 checks. Fresh guest `run-8td2212t` passed 57 records, retaining
all prior 56. Archive SHA256:
`c94e86780a4a056f1db8521fb8de5ebdbb34d3ec127687d134b4a95abf03930b`.
Screenshot and bound receipt were inspected; normal shutdown, closed disks and
slot release completed in a 21.02-second cold automated cycle. Merged to main.
The native full fixture is exactly 64 programs; later probes must use scoped
fixtures rather than increase production capacity or remove existing tests.
