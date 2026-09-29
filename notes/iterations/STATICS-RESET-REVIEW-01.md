# STATICS-RESET-REVIEW-01: candidate blocked

Reviewed candidate: `b2d003b6820d6c19b47885986e8271f5b2d049fe`.
Reviewed main: `6cd71fefc50167e1161c590a2f8f515bfb861e1a`.
Date: 2026-09-27. This is a review record, not an implementation or acceptance.

## Findings

1. **Retained cat buffer can become undersized.** `raw_cat()` allocates only
   while its function-local `buf` is NULL. `main()` still assigns global
   `bsize` on every -B option. Leaving both globals unmanaged therefore does
   not keep them consistent. A disposable candidate export running its
   sanitizer binary first used -B 2048 and then -B 4096 on a 3002-byte file.
   The retained log reports a WRITE of 3002 bytes past a 2048-byte allocation.
   The worker reported exit 1; the coordinator independently inspected the
   retained diagnostic and corresponding source. The fixture was two lines,
   each 1500 repeated letters plus newline, copied to an output file twice.
2. **Kernel teardown leaves dangling command caches.** A diagnostic launcher
   called the normal create/register/boot/run/destroy sequence twice in one
   host process. The first heap-buffer cat invocation succeeded; the second
   produced a retained sanitizer diagnostic for a write through freed memory.
   `sr_program_destroy()` frees the arena without clearing the function-local
   pointer. This control changes only the diagnostic launcher, not the fix.
3. **Compile-time defaults do not survive kernel recreation.** The wrapper
   zeros managed slots after execution and captures the next program's defaults
   from those live slots. A retained two-kernel diagnostic showed ls termwidth
   starting at 80, becoming 0 after the first run and remaining 0 in the second.
   This directly checks stored defaults; it is not a claim that every ls mode
   visibly fails.
4. **Regression detector can report false success.** The coordinator executed
   the exact candidate `tests/test_statics_repro.sh` against both false and true
   programs. Both returned status 0 and reported `5 fixed, 0 still broken`.
   The script prints command status but does not require it, and only rejects
   selected diagnostic substrings. Positive command results are never required.

The first control ran on the worker's pinned Linux build image (reported image
prefix `7618701ca718`). No Mac or Solaris execution was performed. The
coordinator inspected the retained logs and diagnostic launcher after the
worker's final handoff failed; the worker's pre-error messages and log contents
are the evidence, not a completed live-target review.

The branch also includes unmerged LS-02 and predates later main utility/build
changes. Preserve it; do not replace current main's tree with this snapshot.
Interleaved cache ownership still needs a focused test; no reproduction is
claimed for that additional source-level concern.

## Required follow-up

STATICS-REPRO-GATE-02 owns detector corrections. STATICS-CACHE-02 owns the
buffer/default lifecycle repair and meaningful regression cases. Neither the
candidate's green #485 nor a repaired detector substitutes for exact fresh
Mac and Solaris qualification. No merge is approved by this review.

## Retained evidence hashes

Raw local review logs are retained under the coordinator's temporary
`cannedbsd-statics-review-evidence` directory. Hashes distinguish those files
from later reruns; they are not source-archive or runtime acceptance hashes.

- `cat-resize.log`: `f4ef86596f70ba77be77d2161aa25bee978b8497a20b479c44463d380e8c818a`
- `two-kernel.log`: `c62456f8aa2493853a16e543e4fe2f3efce82150bdeeafe2a4aaf09a3e63023f`
- `defaults-two-kernel.log`: `e8962ebe73e2f98b1e18f1570d0ee180192813be27226bbc04c22788299676a6`
- `detector-false.log`: `de5d466d9cb481a6e201c1e238b5beff2609e5e9e6caf8de3075e2437e2fca09`
