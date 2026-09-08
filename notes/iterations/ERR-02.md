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
