# FWRITE-01-design: complete output elements for head

Design only, based on accepted runtime `6f860c4`. The reviewed HEAD-01 source
inventory identifies pinned head's `fwrite(buf, 1, bytes, stdout)` call and its
short-result failure branch, followed by `feof(stdout)`. The existing STDIN
plan already specifies that output feof returns zero without changing errno.
Head does not need writable fopen or general buffered streams for this call.

## Implementation boundary

Expose private cb_libc_fwrite through the ordinary stdio veneer, supporting
only the immutable stdout/stderr identities. Preserve their existing per-task
cb_stdio_state_v1 flags, lifecycle, optional accessor and frozen state minimum.
Do not require the new input accessor, allocate memory, change the ABI or
refactor existing output helpers while adding this behavior. The existing
write_all helper discards progress on failure, so it cannot supply element
counts by simply converting its success/failure result.

Process zero size or zero count first: return zero without inspecting pointers,
looking up state, touching errno/flags or making any callback. For nonzero
requests, reject an unknown stream by identity without dereferencing it; return
zero/EINVAL. Require valid existing output state before I/O; unavailable,
undersized or wrong-version state returns zero/ENOSYS with no write or flag
mutation. Reject size > SIZE_MAX / nmemb with zero/EOVERFLOW, and NULL buffer
with zero/EINVAL. Argument errors do not mark a valid stream's write-error flag.
Use the common EOVERFLOW mapping introduced by STDIN-03, avoiding parallel enum
or strerror changes. FWRITE-01 remains blocked until that prerequisite lands.

For a valid request, repeatedly write remaining bytes, clipping each request
to INT64_MAX without narrowing on ILP32. Each positive return advances the
byte pointer/count and is retried; it does not mark error. A negative return
stops with the actual write errno and sticky error on only the selected stream.
Zero progress stops with EIO and the same sticky flag. Do not retry negative
results. Return bytes transferred divided by size, counting complete elements;
a partial last element may have been emitted and is never buffered or replayed.
A full transfer preserves incoming errno even when successful callbacks alter
it. Existing sticky errors remain set after later successful calls. This does
not make raw descriptor write participate in stdio flags.

Example size 4/count 2: returns of 3 then 5 yield 2 elements and eight exact
bytes. Six then zero yield 1 element, six emitted bytes, EIO and a sticky error.
Six then a negative EPIPE result yield 1 element and EPIPE. Size 1 covers head's
byte-count use directly. No blanket success may conceal a short transfer.

## Falsifiable tests and integration

Use finite mocked write sequences to assert exact callback arguments, byte
prefixes, element counts and flags: full, multiple positive short writes,
zero before progress, partial then zero, partial then negative, and success
after an existing sticky error. Distinguish stdout and stderr in the same task
and preserve task interleaving/exec coverage from accepted STDOUT-01. Include
nonzero incoming errno sentinels and state accessors that disturb errno.

Independent outer old-size, NULL callback, NULL state, short state and bad
version cases must reject before the first write; restoring the valid binding
recovers unchanged flags. Input-state absence alone must not prevent output.
Test zero requests with unusable pointers and no state callback, actual ILP32
and LP64 size overflow, invalid streams and NULL nonzero buffers. Do not allocate
an enormous buffer to exercise an arithmetic rejection.

Add an ordinary private-symbol probe for binary bytes including NUL and 0xff,
stdout/stderr separation and zero-size no-op. Register it in both native shared
Mac fixtures and the guest build; preserve every existing case and the fixed
program capacity. A focused regression control that wrongly treats the first
positive short write as completion must fail the exact-count/bytes assertion;
a control that suppresses sticky error must fail independently. Label controls
run after implementation honestly rather than claiming preimplementation red.

Implementation requires clean independent review, all three exact Woodpecker
checks and a fresh exact-artifact Mac run with screenshot and normal shutdown.
This note introduces no runtime behavior and requires no guest execution.
