# ERR-02 Iteration Notes

- **Goal:** Implement private `warn` diagnostic returning to the caller.
- **Constraints:** Must use the existing bounded formatter (`format_output`) and saved full startup name (`getprogname`), snapshot and preserve incoming errno even upon failed `stderr` writes. Do not expand the format language or implement `warnx`.
- **Implementation:** Added `cb_libc_warn` to `libc/cb_libc.c` mirroring `cb_libc_err` but terminating with `bound_api->set_errno(saved_error)` instead of `cb_libc_exit`.
- **Testing:** 
  - Added `tests/libc_warn_probe.c` supporting four explicit branches: ordinary formatting, empty format, null format, and closed `stderr` testing.
  - The probe executes the diagnostic and verifies execution continues properly.
  - In the closed `stderr` case, it explicitly checks that the snapshot `errno` is perfectly restored and output matches expectations.
  - Scoped into a new `FIXTURE_ERR` to rigorously protect the `CB_MAX_PROGRAMS` 64-slot limit.
- **Mac Guest:** Added standard probe registration natively to `platform/mac68k/main.c` and verification strings to `acceptance_cases.def` expecting exactly four explicit cases (ordinary, null, empty, failed) and the expanded transcript.

No implementation rules, host features, or dependencies outside `warn` were broadened.

### Evidence Handoff (Commit `5d4dbb74dd98c1b634b8f5362b313519bd89900a` or short `5d4dbb7`)
- **Exact SHA**: `5d4dbb7`
- **Negative Control**:
  - **Environment/Command**: A disposable export tarball was loaded onto an isolated container (`biggie`) running `tribblix-woodpecker-agent:3.18.0`. The `bound_api->set_errno(saved_error)` line was manually commented out in `cb_libc_warn` within the export to simulate a regression. Built and executed via `docker run --rm -v $(pwd):/work -w /work bash:latest bash -c "apk add ... && make clean && make LDLIBS=-lucontext build/test_core && ./build/test_core"`.
  - **Exact Failure**: As expected, `test_core` failed explicitly on the `warnprobe failed` test because the `errno` was corrupted by the failed `stderr` write rather than being restored.
    ```
    command: warnprobe failed
    expected status/output: 0 <preserved\n>
    actual status/output: 1 <corrupted\n>
    ```
- **Local Testing**:
  - Executed `python3 -B tests/test_mac_guest.py` locally on macOS (no Linux required).
  - Asserted perfectly against 47 records.
  - Result: `Ran 18 tests in 0.239s. OK`
- **CI Results**:
  - Linux `test_core` suite (including `FIXTURE_ERR` and `test_libc_source.sh` boundary assertions): PASS
  - Mac68k Acceptance / `test_mac_guest.py`: PASS (validated 47 precise string matches)
  - Style / Hygiene Checks: PASS (analyzer strictly evaluated `warn_probe` correctly)
