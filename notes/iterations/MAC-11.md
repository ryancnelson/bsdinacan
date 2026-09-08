# MAC-11: refuse launch into a locked desktop

- Status: implemented; exact feature CI and coordinator guest gate pending.
- Base SHA: `f301464`.
- Branch: `work/MAC-11`.
- Hypothesis: a session gate before matcher startup and immediately before guest
  launch can reject a positively locked desktop without rejecting the normal
  unlocked dictionary, which omits the lock key.

## Red

- Added startup-boundary tests before the implementation.
- Command: `lua tests/test_mac_runner_startup.lua`.
- Expected and observed failure: `desktop preflight must fail closed: screen is
  locked`. The old runner remained active with a matcher despite a locked
  session. Existing readiness tests passed first.

## Green

- `lua tests/test_mac_runner_startup.lua`: passed existing readiness/autorun
  startup checks plus initial/rechecked locked, unavailable, throwing,
  malformed, off-console, and incomplete-login sessions. Both omitted and
  explicitly false lock keys permit an active, complete login.
- `lua tests/test_mac_autorun_driver.lua`: passed.
- `luac -p` for `run.lua` and `desktop-preflight.lua`: passed.
- Read-only Hammerspoon call of the pure gate on the actual current session
  returned `{"allowed":false,"reason":"screen is locked"}`. This did not run
  the driver, launch/control the guest, or alter the claimed slot.
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in Alpine 3.22
  with build-base, clang20, compiler-rt, libucontext-dev, Python and ripgrep:
  passed (exit 0), including sanitizers. Raw local log is retained outside
  the repository; exact Woodpecker verification remains separate.
- Exact Woodpecker `ci`, `mac68k`, `mac-automation`: pending.
- Guest acceptance: pending coordinator slot and unlocked desktop; this host
  gate's deterministic checks are not a guest runtime acceptance claim.

## Change and review

The helper consumes the documented `hs.caffeinate.sessionProperties()` API via
`pcall`. A true `CGSSessionScreenIsLocked` rejects launch; a missing or false
lock key is accepted only alongside true `kCGSSessionOnConsoleKey` and
`kCGSessionLoginDoneKey`. Missing/non-table data, a query exception, a missing
API, and malformed or unconfirmed fields fail closed with a bounded reason.
The initial check occurs before matcher creation, and the second check follows
matcher readiness immediately before creating the launch task.

Rejection uses the existing failed-run receipt path, stops only the owned
matcher when one exists, and retains the slot and empty guest evidence. Tests
check zero guest launches, no acceptance publication, an explicit failure
reason, and no screenshot claim when no guest window exists. No guest code,
ABI, artifact format, shutdown path, or shared backlog file changed.

Sources: the [Hammerspoon API documentation](https://www.hammerspoon.org/docs/hs.caffeinate.html#sessionProperties)
explicitly says table keys vary with system state and that errors return nil.
The [implementation](https://github.com/Hammerspoon/hammerspoon/blob/master/extensions/caffeinate/libcaffeinate.m)
passes through `CGSessionCopyCurrentDictionary()` rather than normalizing lock
keys. The observed locked dictionary had all three exact keys above; the
unlocked omission is preserved deliberately, not treated as an unknown lock.

Remaining bound: this is a launch preflight, not an atomic lock reservation.
The desktop can lock after the last check. Existing timeout/focus handling must
retain the guest and slot for coordinator inspection; this change never
unlocks, wakes, or force-terminates anything. Unknown future dictionary schemas
require review and are explicitly rejected.
