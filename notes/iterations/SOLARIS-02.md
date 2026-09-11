# SOLARIS-02: serialized exact-commit Solaris qualification runner (in progress)

Base: `4060ab0` (`work/SOLARIS-01` tip), an explicit dependency-base
override per the coordinator's own instruction -- SOLARIS-01 was frozen
for independent review at that point and not yet merged to `main`, so
this branch is based directly on its tip rather than waiting for a
`main` merge. Branch pushed early to claim `work/SOLARIS-02` per
`AGENTS.md`'s race-avoidance convention. (The coordinator has since
reported `main` merged SOLARIS-01 with independent review and a fresh
Mac pass; this branch's own base is unaffected by that and is not
rebased here without separate instruction.)

**Status: offline runner + deterministic mocked tests only. No live
rig trial has been attempted with this runner.** Per the coordinator's
explicit "pause further live media/staging trials until these are
fixed and tested offline" instruction, all work below was developed
and verified without touching the real Solaris 9 guest, using a real
local directory standing in for the rig's filesystem (genuine `mkdir`
atomicity, genuine file I/O) plus targeted mocks for the genuinely
guest-specific steps (`console.py`/`mon.py` driving an actual QEMU
guest).

## What this branch adds

- `tools/lib/solaris9-qualify-lib.sh`: the actual logic, as small,
  independently testable functions (`sq_quote_remote`,
  `sq_gen_token`, `sq_archive_source`, `sq_acquire_lock`/
  `sq_release_lock`, `sq_stage_iso`, `sq_swap_media`,
  `sq_mount_and_extract`, `sq_start_build`/`sq_poll_build`,
  `sq_fetch_exit_status`/`sq_fetch_transcript`/`sq_verify_pass`).
  `sq_rsh`/`sq_rscp` are stubs here; the driver installs real
  SSH/SCP-backed implementations, and the test harness installs mocked
  ones -- bash resolves function calls dynamically, so no dependency-
  injection machinery is needed for this to work.
- `tools/solaris9-qualify.sh`: thin driver. Validates required
  `SOLARIS_*`/`OWNER_LABEL` environment variables, wires up real
  transport, and calls the library functions in order: acquire lock →
  archive source → stage ISO → swap media → mount+extract on guest →
  capture toolchain identity → start build (detached) → poll for
  completion → fetch exit status + transcript → assert pass → release
  lock (only on a *confirmed* outcome, never on a timeout with
  uncertain guest state).
- `tests/test_solaris9_qualify.sh`: 26 offline, deterministic
  assertions, all passing. See "What was actually found and fixed"
  below for what each category caught.

## Review findings addressed (each with a specific fix, not a blanket rewrite)

The coordinator's WIP review of the first draft found real, specific
bugs; each is fixed and now has a regression test:

1. **Racy, substring-based lock.** The original check read
   `coordinator.lock/owner.txt`, then separately wrote it -- a real
   check-then-act race, and the "is this already ours" test was a
   substring match against a free-text owner label, which could both
   falsely treat an unrelated lock as already ours and fail to
   recognize a real one phrased differently. Fixed: ownership is now
   decided purely by whether a remote `mkdir` on a fixed path
   (`coordinator.lock/holder`) succeeds -- atomic, and never depends on
   parsing free text to decide "is this mine". `owner.txt` inside that
   directory is written only after the atomic `mkdir` already
   succeeded, for human-readable identification, not for the actual
   decision.
2. **Substring `OWNER_LABEL` implicitly "borrowing" SOLARIS-01's lock.**
   Structurally resolved by (1): acquisition never inspects the
   existing owner text to decide whether to proceed.
3. **`eject -f` before verified guest unmount.** Reordered:
   `sq_swap_media` first asks the guest itself to unmount `/mnt` and
   confirms (via a fresh `mount | grep` check, not merely trusting
   `umount`'s reported exit code) that it is actually gone, *before*
   ejecting at the monitor level; then verifies `info block` reports
   "not inserted" before inserting the new ISO, and re-verifies
   `info block` shows the new filename attached before returning.
4. **`cpio | tail` / `mkisofs | tail` masking real failures.** Neither
   pipes through `tail` for its exit status anymore: each command
   writes to a file, its own `$?` is captured immediately, `tail` is
   used purely for human-readable log output afterward, and an
   explicit `*_EXIT=<n>` marker is checked before proceeding.
5. **Second-resolution `run_id` collision risk; `rm -rf`+`mkdir -p`
   guest dirs that could silently overwrite same-run data.** `run_id`
   now includes the process PID and 4 random bytes
   (`sq_gen_token`), not just a timestamp. Every directory this runner
   creates (local evidence dir, remote isostage, remote guest
   extraction dir) uses a plain, non-`-p` `mkdir` and aborts loudly on
   a collision instead of reusing or deleting anything.
6. **A synchronous ~20s timeout around starting the build, which could
   leave real guest work running unobserved.** The build is started
   detached (`nohup ... &`) and polled with short, separate calls
   bounded by a generous overall deadline (30 minutes); `sq_poll_build`
   returns a distinct status (`2`) for "deadline reached, guest state
   still uncertain", and the driver's own `case` on that status
   explicitly skips releasing the rig lock on that path -- a timeout
   never falsely reports completion or frees the lock for a
   possibly-still-running guest.
7. **Unquoted/naively-quoted remote arguments.** `sq_quote_remote`
   POSIX-single-quote-escapes every value embedded in a remote command
   string (paths, owner labels, tokens), verified by an actual
   round-trip test through a real shell for both an embedded apostrophe
   and an embedded newline -- not merely inspected. (An earlier version
   of this function, built from a bash `${var//pattern/repl}`
   substitution, produced subtly wrong output that the round-trip test
   caught immediately; rewritten via `sed`, which is unambiguous here.)
8. **Commit validation.** `sq_archive_source` requires
   `git cat-file -e "<ref>^{commit}"` to succeed before archiving
   anything, rejecting a ref that resolves to a non-commit object
   (or nothing) rather than silently archiving whatever `git archive`
   is willing to produce.
9. **Hash verification against silent transfer corruption.** After
   `scp`, `sq_stage_iso` re-hashes the file on the rig side and
   compares against the hash computed locally before transfer,
   rejecting a mismatch before ever extracting or building an ISO from
   a possibly-corrupted copy.

## Deterministic mocked test coverage (all 26 passing)

- Quoting: apostrophe and embedded-newline round-trip through a real
  shell.
- Lock: first acquire succeeds; a second acquire with a different
  token is rejected (competing owner); release with the wrong token is
  refused and leaves the lock in place; release with the correct token
  succeeds and actually removes the marker; a fresh acquire succeeds
  again afterward (clean recovery, not a permanently poisoned lock).
- Malformed lock (an existing `holder/` directory with no readable
  `owner.txt` inside): treated as held, never auto-cleared.
- Source identity: a non-commit object is rejected, with no tar file
  produced; a real commit is accepted and its resulting tar contains no
  `pax_global_header` contamination.
- Staging: a corrupted transfer (silently modified after copy) is
  rejected via the hash check; a real `mkisofs` failure (via a fake
  binary shadowing the real one on `PATH`) is surfaced as a genuine
  failure, not masked by `tail`; a second stage attempt reusing the
  same run id is refused rather than silently overwritten. (Two
  mkisofs-dependent happy-path cases are skipped when `mkisofs` is not
  installed locally; both were separately, manually re-verified with a
  real `mkisofs` on the CI runner host, `biggie`, without touching the
  Solaris rig -- exit 0, valid ISO produced, hash matches.)
- Media swap: refuses to proceed if the guest still reports `/mnt`
  mounted; proceeds and verifies block state at each step once a
  confirmed `UNMOUNTED` state is seen.
- Polling: returns the "uncertain, deadline reached" status if the
  guest process never disappears in time; returns "confirmed gone"
  once it actually does.
- Verification: a nonzero exit status fails even if the marker text
  happens to be present somewhere; a zero exit status fails if the
  marker is absent, or present only as a partial/substring match; only
  an exact, whole-line marker plus a zero exit both together pass.

## Not yet done

No live rig trial with this runner -- that remains explicitly paused
pending this offline round's review. `notes/CI.md`'s own eventual
`solaris9` Woodpecker CI status is not implemented here; this is
still the "smallest repeatable local runner" that a future CI
integration would wrap, per SOLARIS-02's own Accept criterion.

## Rig connectivity, checked read-only, not assumed unchanged

A read-only connectivity check (not touching media, not claiming or
altering the lock) found the rig's SSH endpoint currently unreachable
(`ssh: connect to host ... port 22: Operation timed out`, on two
attempts with different connect timeouts). This is reported as
observed, not diagnosed further -- no live troubleshooting was
attempted, consistent with pausing live rig interaction during this
offline round. Prior rig state (the coordinator.lock owner reflecting
`work/SOLARIS-02` continuation, drive7 holding an earlier test ISO)
should not be assumed still accurate until connectivity is confirmed
again.
