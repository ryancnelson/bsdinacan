# STDIN-03: unbuffered fread element counts

Base: freshly fetched main `0e35e50ba91bd78284b3333dcf4ffb75441637cf`.
Branch: `work/STDIN-03`. This is stage 3 of the reviewed STDIN-01 design.
The coordinator accepted the base's 59-record Mac transcript. This change adds
only fread and its required EOVERFLOW mapping; buffering, fwrite, and additional
stream ownership operations remain separate tasks.

## Contract and implementation

A zero size or count returns zero before any API callback, pointer validation,
errno access, or multiplication. A nonzero request resolves a live input stream
once, then checks multiplication overflow and NULL buffer before consulting EOF.
Invalid streams retain the existing EINVAL/ENOSYS classification. Overflow uses
CB_EOVERFLOW/private EOVERFLOW 84 and the exact strerror text
`value too large to be stored in data type`, matching the pinned NetBSD mapping.
Argument errors neither read bytes nor change stream indicators.

The same bounded input helper now serves getc and fread. Fread accumulates
positive short transfers until the requested byte count, EOF, or error, returning
only completed elements. Bytes in a partial final element are consumed and left
in the caller's buffer; they are not retained for the next call. Successful
transfers and EOF preserve incoming errno, while read errors preserve their
reported errno. EOF and error indicators remain independent and sticky; a later
successful read after an error is allowed. Existing EOF prevents another read.
The helper rejects positive over-returns with EIO before narrowing the callback's
64-bit result to size_t. Each request is clipped to INT64_MAX only when size_t
can represent a larger value, including the actual 32-bit target boundary.

No ABI table/state layout, tracked allocation, descriptor ownership, exec/reset,
or output-state behavior changes. Existing stdin stage-1 and dynamic stage-2
size guards remain authoritative.

## Focused evidence and negative controls

The disposable source archive SHA-256 before this note was added is
`7356cb53feb71c3d30c23aeb000433dfb76a7941911f7414b88170f72ecf0997`.
Testing uses `tribblix-woodpecker-agent:3.18.0` (Alpine 3.22.5, GCC 14.2,
Clang 20.1.8) in a disposable Linux container with network disabled.

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --fread
```

The corrected snapshot returned zero with `fread tests passed`. The first compile
caught an unsupported ordinary-source memset call; replacing it with the already
supported private memcpy fixed that setup error. That compiler failure is not
behavioral red evidence. The source/object fence now rejects host memset and
requires the ordinary fread/getc/feof/ferror/strerror/errno/memcpy boundaries.

Two separate disposable snapshots exercise after-implementation regression
controls, not tests claimed to precede implementation:

- Insert an unconditional break after fread's first positive transfer. The same
  focused test fails with `FAIL: freadprobe status 40`, exit 1, because the
  ordinary full-element case requires accumulation across 3-byte and 5-byte
  transfers. Altered libc source SHA-256:
  `68553a184c11569092a65d4bcbb14fa7e86a9a5cd5588dadded1c5a145a6d49e`.
- Add an errno getter callback to the zero-size/count branch. The zero-request
  callback counter rejects this with `FAIL: freadprobe status 52`, exit 1,
  before any reads or state access. Altered libc source SHA-256:
  `15bef2ab6221b7b83bf66d8dff2c41b776ace83c7727baf52c6688befe68e464`.

The unchanged good libc source SHA-256 is
`88d5473433cc19a4180ca1a1b93237e44c2daf5b2d94383ed203410a86e93698`.

## Shared native and Mac coverage

The ordinary probe checks exact requested byte counts and buffer offsets,
completed-element versus byte counts, binary 0/255, partial EOF/error, canaries,
error recovery without retained fragments, overflow before NULL/EOF, invalid
streams, closed stdin, and a real RAMFS file shared between fread and getc.
A no-write read adapter checks SIZE_MAX/INT64_MAX request clipping without
pretending to allocate or transfer enormous buffers. A 4294967304-byte bogus
callback result exercises rejection before narrowing on ILP32.

Each sequence runs in its own real child task, and the native top-level cases
use fresh kernels. Zero-request probes count all relevant API callbacks. The
compatibility probe independently tests an actual short outer table, null
callback, null returned state, wrong returned version, actual short state,
stage-1 state, and partial stage-2 state. Legacy stdin remains usable while a
live dynamic stream is rejected without offset/state mutation, then successfully
read after restoring the full binding. Every temporary binding is restored
before its table/state storage is released. Existing lifecycle tests continue
covering task isolation, exec/reset, and stream cleanup.

All 59 prior shared Mac records remain. Checked registration and linking of
`freadprobe` and `freadcompat` make **61 PASS records plus ALL PASS**. The native
fixture stays scoped; the production 64-program capacity and general fixture
are unchanged.

## Validation state

`python3 -B tests/test_mac_guest.py`: 18 tests passed.
Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed, including
native/shared tests, ASAN/UBSAN, private-source fences, publication checks,
clean build-mode isolation, protocol tests, and GCC static analysis.
Exact feature Woodpecker and coordinator-owned exact-artifact Mac acceptance:
pending. No guest result is claimed by this worker.

## Coordinator acceptance, 2026-09-08

Integrated with the other reviewed head prerequisites at
`ff08dd5c22f6c74de0bed5afce7a9798d666b84b`. Exact Woodpecker #301 passed all
three workflows. Fresh guest `run-3c7aibdj` passed all 62 records; the full
transcript and screenshot were inspected. Cold automated cycle: 22.43 seconds,
including normal app/guest shutdown, verified closed disks and slot release.
Artifact archive SHA256:
`f863c4c70f2518c9bf4f23f6aab33f147eb4cc62b50bc80f01e4ec22d8770e37`.
Merged to main only after these gates. The first CONV integration attempt
failed protocol tests because its manifest contained an extra blank line;
removing that line restored all 18 protocol tests before this accepted build.
