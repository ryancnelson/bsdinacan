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

## Coordinator acceptance

Runtime worker `5d4dbb74dd98c1b634b8f5362b313519bd89900a` passed all three
Woodpecker #263 checks. Integration `6f860c426e8842119615efd289f6bee2bcf6b678`
passed independent review and all three #266 checks, retaining echo/getopt and
all prior tests. ERR fixture 7 is distinct from YES fixture 6.

The worker's after-implementation negative control removed the final errno
restore in a disposable export; its captured build/test log was inspected and
shows `warnprobe failed` returning 1 with `corrupted` instead of 0 with
`preserved`. This is regression-control evidence, not a red-first claim.
Local orchestration tests passed all 18 cases against the merged 54-record list.

Fresh guest `run-4j885q64` passed all 54 records. Archive SHA256:
`21a82baf3c465dd6386be50ea66c59573ddd210d3488adbe0ba1fa1279c81ef7`.
Screenshot and receipt were inspected. Cold automated completion took 24.82
seconds with normal app/guest shutdown, closed disks and slot release. The
runtime integration is merged to main. This evidence note changes no runtime.
