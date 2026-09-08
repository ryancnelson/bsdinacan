# STDOUT-01 integration and test repair

Base: accepted main `5906b9c`; merged worker candidate `d1ba766` into isolated
`work/STDOUT-01-integration`. No main merge or guest acceptance is claimed.
This note replaces the worker draft's unsupported green/test-execution claims.

## Observed red and bounded repair

Worker `a02fcb6` failed exact Woodpecker #217: the shared native Mac suite
could not find stdiooldtable (status 127). Worker `d1ba766` failed exact #221:
`build/test_core` printed `AFAIL: stdioprobe boot`. Its new suite tried to
boot one kernel repeatedly; only the first case ran. Other review findings
included missing native registration, host-injection assumptions in the guest
wrapper, missing old stdioprobe fixture registration, uncalled interleaving,
confounded malformed-state checks and no private-symbol check. These are real
failures in an existing implementation, not a claim that this entire feature
was developed test-first.

The integration preserves every accepted test and all 38 previous Mac records.
Four stdio records make 42. The optional accessor follows basename in the
actual API tail. Runtime state remains task-owned, zeroed at creation, reset
on successful exec and preserved on failed exec; the mandatory prefix stays
unchanged. All state reads and writes use one validated returned pointer,
version and size. State lookup and error recording preserve errno, including
callbacks that disturb it. Legacy output remains available without the new
state; new functions reject unavailable state before output.

The old-table wrapper is shared with the Mac application. It independently
checks old API size, NULL callback, NULL returned state, short state with a
valid version, and wrong version with a full size. A counting writer proves
new output never starts and canaries prove malformed flags are not changed.
Existing puts remains callable under degraded tables. The wrapper restores
the full binding while its copy is alive, including rejection paths. Rebind
persistence uses real closed-descriptor EBADF, so no host-only injection is
needed in the guest.

Native tests use one fresh kernel per case and a checked, small registration
set. An API writer injects short writes followed by EPIPE, complete short-write
retries, one-shot failure then success, and zero progress. Assertions inspect
exact stdout/stderr bytes, callback counts, return values, errno and flags.
They cover puts/printf/fprintf/putchar, unsigned-byte conversion, unbuffered
flush-all, invalid pointers, raw-write nonparticipation and formatter-only
errors. A scheduled parent and child create opposite stream errors through
real libc writes and retain independent flags across yield/wait. Failed exec
preserves flags; the successful replacement verifies clean state. No new
program is added to the full fixture or production capacity.

Ordinary probes use main and private public-spelling headers. Source checks
require prefixed putchar/fflush/ferror imports and reject host symbols. The
same shared Mac-safe objects are explicitly registered and linked in both
harnesses. Existing stdioprobe coverage stays in its original fixture.

## Verification

The 18 host artifact/staging tests pass with the 42-record transcript.
Publication hygiene, shell syntax and staged diff checks pass. Exact native,
sanitizer, analyzer and Mac build evidence will come from the pushed integration
commit's Woodpecker workflows. Guest acceptance remains coordinator-owned.
