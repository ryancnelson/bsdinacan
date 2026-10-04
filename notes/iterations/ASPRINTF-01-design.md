# ASPRINTF-01-design — allocating legacy-option conversion for uniq

## Scope and evidence

Design only, based on `f4e3f2544772ad1cae7e17396e0449ee27e10f9d` in
`work/ASPRINTF-01-design`. No implementation, headers, imported files or runtime
behavior change. NEXT-UTIL-03 supplies the dependency investigation; this note
specifies its independently assignable allocating-format prerequisite.

Re-fetched the unchanged `usr.bin/uniq/uniq.c` at NetBSD pin
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. SHA256 remains
`78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`.
Lines 232–255, `obsolete`, walk option arguments until a non-option or `--`;
only a digit at index 1 triggers conversion. The actual call at line 249 is:

```c
(void)asprintf(&p, "-%c%s", ap[0] == '+' ? 's' : 'f', ap + 1);
if (!p)
    err(1, "malloc");
```

`-3` becomes `-f3`; `+3` becomes `-s3`. The return value is ignored, and `p`
starts uninitialized: assigning NULL on failure is essential. The allocated
pointer replaces the argv entry and is not explicitly freed by uniq.

`git grep -n asprintf f4e3f25 -- libc include` and the same search against
fetched `origin/main` yielded no matches. The only matching implementation-task
branch at inspection was this design branch. This confirms no current declaration
or definition in those trees, not the absence of unpublished work. Recheck symbol
ownership before implementation. Candidate `libc/cb_libc.c:1605` implements
snprintf separately: it has no `%c` and uses an unchecked int length accumulator.
Do not build allocating formatting on that parser or expand it as part of this
item. `cb_libc_malloc`/`cb_libc_free` already dispatch through the task allocator;
libc binding requires the allocate/resize/release capability prefix.

## Proposed bounded contract

The implementation owner adds `cb_libc_asprintf(char **out, const char *fmt, ...)`
and the private stdio `asprintf` mapping, coordinating both declaration locations
with the FILE worker. Serialize implementation edits to cb_libc.c and stdio.h
with writable-stream work even though these design notes can proceed in parallel.
No FILE layout or program ABI change is required.

Support literal bytes, bare `%c`, and bare `%s` only. Do not silently inherit
stream formatter conversions. Reject `%%`, trailing `%`, widths, precision,
flags, length modifiers, numeric and other conversions with `-1`/`EINVAL`.
A later measured caller can justify additional syntax. Null format or null `%s`
argument also fails with EINVAL. For non-NULL `out`, set `*out = NULL` before
validation; null `out` fails EINVAL without dereferencing. The caller supplies a
writable output pointer and valid, stable NUL-terminated input strings. Existing
storage formerly referenced by `*out` is the caller's responsibility; do not free
it. On success return byte length excluding final NUL, publish newly allocated
storage, and preserve incoming errno. Empty output owns a one-byte NUL allocation.

Consume `%c` as `int` (default promotion of the actual character expression),
convert to unsigned char and emit exactly one byte, including NUL. Consume `%s`
as a character-string pointer (using `const char *`, as the existing formatter
does); never fetch an integer-sized stand-in for a pointer. `%s` stops at NUL.
An embedded `%c` NUL counts toward the return value; following bytes and the
final terminator still exist and require bytewise tests, not strlen assertions.

Use one small private parser in two passes with independent va_lists. A
`va_copy` from the original list is appropriate; call va_end on each initialized
list on every path and probe compiler support on the required targets. No shared
scratch buffer or persistent formatting state. The first pass validates and
counts without allocating. Check each size_t addition before performing it,
using `piece > SIZE_MAX - total`; also enforce total <= INT_MAX and total <
SIZE_MAX before allocating `total + 1`. Overflow fails `-1`/`EOVERFLOW` with
NULL output. Avoid unchecked strlen-then-int conversion: bounded scans can stop
once the remaining permitted result length is exceeded.

For the measured format and a suffix of length n, result length is n+2 and
allocation size is n+3. Require n <= INT_MAX-2 and n <= SIZE_MAX-3 using guarded
arithmetic. The general literal/c/s parser applies the same checked additions.

Allocate exactly the checked result length plus terminator through
`cb_libc_malloc`, never host malloc. A failed allocator leaves output NULL and
preserves its failure errno (ordinary exhaustion is ENOMEM). Emit with an
explicit capacity bound in the second pass; reject and release any unexpected
count/validation inconsistency rather than overrunning. Publish only after
successful termination. No new yield is introduced. The supported task model
requires inputs to remain stable for the call; this is not a concurrent-mutation
API. Any post-allocation failure releases through cb_libc_free and restores the
intended error. Successful allocation is owned by the calling task: explicit
free, successful exec, exit and teardown follow existing heap rules. A peer may
not free it. Uniq's unfreed rewritten option is therefore reclaimed on exit,
before zombie reap. No process-global ownership list or argv special case.

## Falsifiable implementation loop (not executed here)

1. Add an ordinary private-header probe that calls the exact `"-%c%s"` form.
   Baseline must fail for undeclared/missing asprintf, consistent with the
   measured uniq diagnostic. Once the interface is introduced, require `-3`
   -> `-f3` and `+3` -> `-s3`, length 3, final NUL, and freeable task allocation.
   Also run the actual unchanged obsolete function in a bounded fixture (extract
   its exact source range with a recorded hash, or use the eventual pinned uniq
   build once fgetln/FILE exist). Do not fake the other missing interfaces merely
   to make the full uniq compile. Label direct-call tests separately from actual
   command execution.
2. Cover empty suffix and literals, several c/s conversions with distinct later
   arguments, empty output, long strings, and embedded NUL `%c` using memcmp plus
   canaries. The real obsolete fixture additionally preserves `--`, non-option,
   and non-digit argument behavior. Do not claim full uniq acceptance yet.
3. Initialize output to a sentinel; inject allocator failure and require -1,
   NULL, ENOMEM and no live allocation. Reject unsupported forms and NULL inputs
   before allocation; include a valid prefix followed by `%d`/trailing `%` to
   ensure no partially published output. Supply correctly typed arguments for
   all accepted conversions; unsupported tokens must not consume an argument.
4. Exercise the production checked-add helper with synthetic length counts at
   INT_MAX, INT_MAX+1 when representable, SIZE_MAX and final-NUL boundaries.
   This avoids allocating multi-gigabyte fixtures or fabricating invalid string
   pointers. Pair those arithmetic tests with real string/c/s output tests;
   a helper test alone does not prove parser wiring. Verify allocator request
   size is exact with a mock and that overflow performs no allocation.
5. Hold allocations in two interleaved internal tasks, confirm separate contents
   and task ownership, release one, and test exit/successful exec reclamation
   before reap/teardown. Failed exec must retain the allocation. Reuse existing
   heap lifecycle observation rather than inventing a new accounting subsystem.
6. Run focused tests and complete Linux make ci, then exact-commit Woodpecker
   ci/mac68k/mac-automation, independent review, fresh serialized MAC-01 guest
   acceptance including the ordinary-source allocating-format probe registered in
   the shared Mac suite, and required Solaris
   qualification. Missing guest access stays pending, not waived.

This design requires no runtime tests or guest use itself. Verification here is
source/hash inspection and diff checking; no red/green execution is claimed.
Implementation, shared declaration ownership, and runtime acceptance remain
separate assigned work. Only this note is changed; no shared symbols touched.
