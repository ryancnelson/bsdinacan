# SOLARIS-02: offline runner correction

This is a host-runner repair from frozen
`f3a38276f0616afe59f47ce34ab2a7a57f4de8e0`, on
`work/SOLARIS-02-review`. The original worker branch is preserved. It changes
no cannedBSD runtime, private libc, target adapter, or guest acceptance case.
Solaris live acceptance remains pending: no console, monitor, media, guest,
SSH security setting, or shared rig was used to develop these corrections.

## Falsifiable loop

Hypothesis: actual driver ownership and completion can be tested offline, and
must fail closed when the rig's owner or guest's completion is uncertain.
Before implementation, `python3 -B tests/test_solaris9_qualify_driver.py` ran
five new controls against the frozen code. All five failed (exit 1):

- An existing manual `coordinator.lock/owner.txt` did not prevent acquisition.
- A newline in the owner's label let the wrong token authorize release.
- A disconnected poll transport returned success.
- The actual driver's timeout arm and EXIT cleanup released ownership.
- Successful staging returned five stdout lines, including `TAR_EXIT=0` and
  `MKISOFS_EXIT=0`, where the driver expected only ISO path and digest.

The same controls passed after correction. The timeout control now includes
the actual driver's transition into guest-uncertain state. Additional full
actual-driver tests avoid relying solely on that extracted cleanup fragment.
The additional parser, generated-command and full-driver cases were added
while repairing the runner; they are regression coverage, not claimed as
pre-implementation red tests.

## Boundary and ownership

The shared `coordinator.lock` directory itself is acquired with one atomic
`mkdir`. Existing legacy, empty or malformed locks remain held. A separate token
file must match every byte for release; free-text labels never authorize it.
Unknown files prevent removal of the directory. No stale lock is auto-cleared.
If acquisition's owner-file write is uncertain, the directory is retained.

References are peeled to commits before rig operations. Each run has a random
128-bit suffix and a fresh local evidence directory. Remote staging reserves
its entire directory before transfer and uses noclobber file creation. Archive
hash verification, extraction and ISO creation propagate failures separately;
only the relative ISO path and digest use stdout. Existing run archives cannot
be overwritten by a colliding run name.

Each console call uses a fresh nonce and exact start/end lines with the guest
command's status. The command runs in a child `ksh -c`; an error does not exit
the controlling login shell. Echoed commands, CR/CRLF, prompts and unrelated
console-helper markers cannot substitute for a complete successful frame.
The detached build publishes its run-token/status marker atomically as its
last mutation. Polling requires both absence of its PID and that exact marker;
transport error or empty/numeric output is not completion. Both wall-clock
expiry and a finite attempt budget bound polling, including a stalled clock.

Before ejecting media, a successful guest mount-table query and any required
unmount must establish that `/mnt` is clear. The exact selected drive must be
empty after eject and contain the exact new path after change; another drive
or a path prefix cannot authorize extraction. `find` and `cpio` run separately
with checked statuses. The new guest directory must not already exist.

Once the first guest operation begins, interruption, disconnect, malformed
completion, failed extraction or failed unmount retains the lock. Only a
positively completed build, captured transcript and verified final unmount
permit normal release. A completed failed build can release safely but cannot
pass. A release failure makes the driver's exit status nonzero, even after a
successful build. Acceptance requires exit status zero and confirmed release;
the provisional PASS log line alone is insufficient.

## Validation and limitations

`make check-solaris9-qualify` runs the existing shell controls, full actual
driver with offline transport, and parser/generated-command controls. It is
part of `make ci`; none of these tests needs credentials or network access.
Every scratch path and fake Git repository is unique to one test invocation.

The full driver controls exercise real temporary-directory `mkdir`, `cmp`,
archive, hash and copy operations. They cover annotated/noncommit refs, manual
ownership, ISO-stage failure, quoted paths/labels, verified unmount ordering,
wrong drive, cpio error, uncertain start, disconnect, deadline expiry, malformed
frames, wrong run, interruption, bad exit/PASS marker and failed release.
Eight simultaneous shell contenders additionally assert exactly one owner.
Generated extraction and unmount command bodies execute locally with only
host-sensitive utilities and paths substituted, checking their actual shell
status propagation and postconditions.

ISO contents, QEMU and the Solaris console are simulated in the driver tests;
they are not evidence of a valid ISO or real guest compatibility. The existing
two real-`mkisofs` local tests skip if that program is absent, and must be
reported as skipped. A later coordinator-owned live trial must verify the
actual console framing, QEMU block output, guest toolchain/PATH, full native
transcript, exact archive identity and orderly ownership release. The future
`solaris9` Woodpecker status is not installed by this repair.

This branch's host parser/tests require Python 3.9 or later. The guest commands
retain the existing Solaris `ksh`, `awk`, `find` and `cpio` boundary. No guest
Python dependency is introduced. The driver still requires its documented
host tools and private rig coordinates; the offline fixture supplies neither
actual infrastructure nor acceptance permission.

Exact feature Woodpecker results are reported in the handoff after push.
Mac guest execution is not required for this host-runner-only change. Real
Solaris runner acceptance is explicitly pending the later serialized trial.
