# STDIN-01: task-owned stdin and input indicators

Base: assigned integration `6f860c4`, containing reviewed STDIN-01-design
`5fcc687`. Branch: `work/STDIN-01`. This implements stage 1 only: ordinary
`stdin`, `getc`, `feof`, and input `ferror`. It does not implement `fopen`,
`fclose`, `fread`, buffering, or writable dynamic streams.

## Contract and ownership

The global stdin pointer identifies descriptor zero; it stores no task flags.
An optional API-tail `input_state_location` returns the current task's embedded,
versioned EOF/error state. Its frozen minimum ends at `stdin_error`, so later
input fields can append without changing stage 1's required prefix. The existing
output state layout and accessor requirements are unchanged. An absent outer
field, null callback, null result, wrong returned version, or short returned
state fails before reading with ENOSYS. Invalid stream identities fail with
EINVAL without dereferencing the supplied pointer. `feof(stdout)` and
`feof(stderr)` return zero and preserve errno even without input capability.

`getc` reads one unsigned byte. Zero is data, 255 is distinct from EOF, a zero
read sets EOF, and a failed read sets only the sticky error indicator. EOF stops
further reads; error alone permits retry. Successful reads and EOF preserve the
incoming errno. Descriptor rebinding and failed exec preserve flags; task
creation and successful exec start with clear flags. Existing descriptor
inheritance and close-on-exec policy are unchanged. This stage allocates no
stream wrappers: the input state lives and dies with its task.

## Falsifiable regression control

Hypothesis: an ordinary source reading bytes `00 ff 41` must receive integer
values 0, 255, and 65, and may only set EOF after the next read returns zero.
The new interfaces were implemented before this control; this is an
**after-implementation regression control, not a test-first red result**.

The source snapshot SHA-256 was
`aa3df718bb43535ca468c44493a31c6286de66289ba672aaca7783a23a32a50a`.
In a disposable Linux container, using `tribblix-woodpecker-agent:3.18.0`
(Alpine 3.22.5, GCC 14.2), the good snapshot ran:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --stdin
```

It returned zero and printed `Ostdin tests passed` (`O` is the tested old-size
output capability's byte). A separate disposable copy changed only the successful
`getc` return from `(int)byte` to `(int)(signed char)byte`, preserving all guards.
The same rebuild and focused command then exited 1 with:

```text
stdininject binary status 3
FAIL: stdin ordinary probe
```

Status 3 is the ordinary probe's exact `getc(stdin) != 255` assertion. The good
`libc/cb_libc.c` SHA-256 was
`63ad1212ff258de09db4c89c13f9c7dec430c0da19681874b45cf3f885aaf118`;
the controlled negative file was
`82635852dd3adf6fe1fb0ccc49104c3874998795b6256a3967c1d8673590dc22`.
No negative mutation was applied to the feature worktree.

## Coverage and gates

The focused native tests use a fresh kernel per injected case. They check exact
read counts, byte 255, one evaluation of `getc`'s argument, EOF versus read
failure, retry after EIO, successful callback errno side effects, invalid
identities, and independence from output errors. Shared ordinary probes exercise
real file/pipe descriptors, child task state isolation, inherited descriptors,
failed exec preservation, successful reads after raw rebinding while preserving
the sticky error, and successful exec reset
of both EOF and error. Every child status and setup/exec outcome is checked.

Compatibility tests allocate genuinely short outer and returned-state objects,
keep null callback and null returned state distinct, assert zero reads on every
rejection, restore the real binding before releasing short tables, and exercise
output using an original-sized output state. Private-header symbol checks cover
`getc`, `feof`, `ferror`, the stdin identity, and task errno.

The existing 54 Mac records are preserved; `stdinprobe` and `stdincompat` bring
the expected shared transcript to **56 PASS records plus ALL PASS**. Both are
checked registrations in the actual 32-bit Retro68 application, and the native
Mac fixture reuses the same modules while retaining the 64-program bound.

Local protocol tests: `python3 -B tests/test_mac_guest.py`, 18 tests passed.
Full Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed. A final
test-only assertion then added a successful read after actual descriptor rebinding;
the affected full native `make LDLIBS=-lucontext test` and Clang sanitizer gate
were rerun before handoff.
Exact feature Woodpecker and exact-artifact guest evidence: pending; the
coordinator owns guest staging and acceptance. No guest execution is claimed.

## Coordinator acceptance

Reviewed runtime `935fc83bcf9546a0b34137081ff37ba7f4ff5125` is byte-identical
to integration `27bb7be462f500c99c2fa05fb42c90838da65cab`; remaining differences
are reviewed project/design documentation. All three exact #277 CI checks
passed. Fresh guest `run-q732x7z0` passed all 56 records, preserving the prior
54. Its archive SHA256 is
`4c7a00a082297ab857af2d4133f100cfd6992940f6d7d202a884da5aff4dc453`.
Screenshot and bound receipt were inspected; normal shutdown, closed disks and
slot release completed in a 24.94-second cold automated cycle. Integration is
merged to main. Read-only file wrappers remain a separate next task.
