# MAC-05: resume the actual requester of root-stack services

- Status: implemented; verification in progress
- Base SHA: `9a3e4db6314e7115e7f1ac37ac68faa800a9c9da` (assigned MAC-02 dependency)
- Branch: `work/MAC-05`
- Worktree: sibling `bsdinacan-MAC-05`
- Hypothesis: after root switches to A and A switches to B, a root service
  requested by B must resume B, preserving its local state. Resuming the root's
  original target A instead violates the context contract.

## Red

- Command: `LDLIBS=-lucontext tests/test_mac_root_dispatch.sh` in the existing
  Linux Woodpecker agent image on biggie.
- Added a real alternate-stack root → A → B scenario to the existing test.
- Observed failure: `stage == 4 && dispatch.active == &first` failed in
  `first_entry`, exit 134. A resumed immediately after B's service call instead
  of waiting for the scheduler's later explicit resume. B never reached its
  post-service continuation.

## Green

- Focused command: same test — passed after the fix.
- Existing 32 uninterrupted child service requests remain covered. Additional
  checks prove B receives its result with its local array preserved, later
  resumes again with that state intact, and A resumes only when explicitly
  selected. Nested service calls made on the root execute directly on the
  root and leave no pending request behind.
- Full gate: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` — passed in the
  Linux Woodpecker agent image (optimized, sanitizer, build-mode, architecture,
  publication, and static-analyzer gates).
- Linux Woodpecker: pending.
- mac68k Woodpecker: pending.
- Exact-artifact guest acceptance: pending; coordinator owns emulator slot.

## Change and review

- Each pending service request now stores its requester. The root selects that
  context for immediate continuation before invoking the service callback,
  instead of reusing its original target from an earlier context switch.
- No public ABI or assembly layout changes. Pending requests still live on
  their suspended requester's stack and are cleared before callbacks run.
  The callback reads caller-owned arguments while that stack remains suspended.
- The current kernel switches only between root and one task, so this closes
  the broader nested-context contract without changing normal scheduling.
  MAC-03 and other agent branches were not modified.
- Remaining acceptance: verify the exact CI application in System 7. The native
  test exercises real separate stacks but does not replace the 68K guest gate.
