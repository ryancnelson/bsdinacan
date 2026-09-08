# ECHO-01-design: bounded prerequisites for unchanged NetBSD echo

Design only, based on main `553b10f`. No runtime implementation or guest
acceptance is claimed. The measured source and hashes are in
`utility-roadmap-20260908.md`; the source ignores individual output returns and
checks ferror(stdout) after fflush(stdout).

## Program identity without a redundant setter ABI

The existing mandatory runtime getprogname callback returns current task argv[0].
Task creation already supplies this before ordinary main; successful exec replaces
argv and failed exec preserves it. Expose private stdlib getprogname as the final
slash-delimited component of that value, a borrowed pointer valid while argv is
unchanged. Do not allocate, use libc basename's mutable scratch buffer, or change
argv. Test plain names, paths, empty final components and task/exec isolation.

NetBSD's documented startup initializes the name once; subsequent setprogname
calls have no effect. Adopt that explicit startup-initialized contract: private
setprogname accepts its argument but preserves the established task identity.
This is justified by existing startup state, not an unimplemented mutable setter.
No new runtime callback is needed. Existing raw runtime getter and err behavior
remain compatible; this task does not change their diagnostic naming contract.
Public getprogname/setprogname declarations and mappings remain private.

Reference: [NetBSD getprogname/setprogname](https://man.netbsd.org/setprogname.3).
This deliberate choice supersedes the audit's open question about setter ownership.

## Unbuffered standard output streams

Keep the existing immutable stdout/stderr FILE identities. Append one optional
runtime accessor for a versioned, task-owned standard-output state containing
independent stdout/stderr error indicators. Preserve the existing mandatory API
prefix, append after the actual integration tail, and validate the accessor's
field end and pointer before use. New tasks start clean; successful exec resets
indicators; failed exec leaves them intact. Merely rebinding cb_libc_start must
not clear indicators. No mutable process-global error flags or general fopen,
fclose, input stream, or buffering subsystem belongs in this step.

All existing stdio output paths must mark the appropriate indicator on actual
write failure. Keep partial-write retry behavior; a zero-byte write before
completion is EIO. Successful output preserves incoming errno, so a later
successful write does not erase the prior failure before echo invokes err.
Raw write does not itself set stdio indicators. A successful later stdio write
never clears a previously set indicator. Invalid stream pointers are rejected
before dereference and do not contaminate either valid stream.

putchar writes the unsigned-char conversion of its argument to stdout and returns
that byte, or EOF on failure. ferror returns the current task's sticky indicator
without changing errno for supported streams. fflush(stdout), fflush(stderr),
and fflush(NULL) return zero with errno preserved: no buffered bytes exist to
flush. A prior sticky error stays set and is reported by ferror, rather than
being misrepresented as a new flush operation's failure. This bounded flush
contract does not imply storage durability or validate a raw descriptor that
another operation has closed.

Old-size and NULL-accessor runtimes must still start and run existing operations.
New state-dependent interfaces reject unavailable capability with ENOSYS;
ferror returns nonzero on this unavailable path so callers cannot obtain false
success. Existing output operations retain their old behavior when the optional
state is absent, recording indicators only when available. Document this bounded
extension behavior alongside ordinary supported-stream semantics.

Reference: [NetBSD fflush](https://man.netbsd.org/fflush.3). The no-buffer case is
an implementation decision derived from that operation's purpose.

## Independent worker steps and acceptance

1. PROGNAME-01: public startup-initialized program-name veneer, source checks,
   task/exec/lifetime tests, exact CI and ordinary Mac probe.
2. STDOUT-01: per-task output-error state, putchar/fflush/ferror, and participation
   by existing output functions. Inject partial writes then failure, zero writes,
   stdout versus stderr independence, failure followed by successful output,
   forced task interleaving, successful/failed exec, and old-size/NULL accessor.
   Assert output bytes, status, errno and flags; no teardown-only ownership claim.
3. ECHO-01: import the exact unchanged command after both prerequisites. Preserve
   current shell echo behavior unless an explicit comparison supports replacement;
   initially register the imported utility under a distinct name for acceptance.
   Pin the hash/license and private symbols. Any unused-argc warning exception
   belongs only to this imported translation unit. Include exact streams/status,
   -n and literal operand cases plus a failed-write command regression.

Each implementation needs a falsifiable red test, complete exact Woodpecker
checks and a fresh Mac artifact run. Keep the 64-program limit and scoped
fixtures. Design review should resolve any ABI or error-contract objection
before assigning STDOUT-01; no change here authorizes speculative stream APIs.
