# WRITE-02-linux: finite Linux console write continuations

Base: accepted `e65e36fc0444193445b8a304f6e818d2768d5b62`.
This is the Linux half of the reviewed WRITE-02-design; the portable callback
validation is a separate worker change. No public ABI or Mac source changes.

## Red before implementation

Added `tests/test_linux_write.c` first. It includes the real Linux backend
translation unit with only its write syscall token redirected to a deterministic
callback; there is no second copy of the production loop. Bounded callback
plans check descriptor, buffer offset and remaining count, and abort the test
on any unexpected call rather than hanging on the original zero-progress loop.

In the pinned Linux CI image, GCC with C99/full warnings and libucontext:

```sh
make LDLIBS=-lucontext build/test_linux_write
./build/test_linux_write zero
./build/test_linux_write partial-error
```

Compilation succeeded. Both tests failed with exit 1 before the implementation:

- `FAIL Linux console write zero: unexpected callback after bounded plan (no progress)`
- `FAIL Linux console write partial-error: returned count/error`

These were observed pre-fix behavior failures, not setup failures or mutations
made after implementation. No claim is made that a real terminal caused either
syscall sequence.

## Change and green

A zero or hard error now ends the loop. If an earlier syscall wrote a prefix,
return that prefix so the caller does not lose the count of bytes emitted;
otherwise return the existing EIO mapping. EINTR still retries at the same
buffer position. No global errno or public error mapping changes.

`make LDLIBS=-lucontext check-linux-write` then passed all eight deterministic
cases: first zero, partial/error, partial/zero, multiple successful fragments,
first error, EINTR/success, partial/EINTR/success, and empty input with no
callback. Every case checks exact output and returned count, request offsets,
remaining sizes and callback count; stdout and stderr are both exercised.
The canonical `make ci` now runs this focused target.

Exact Woodpecker ci/mac68k/mac-automation and independent review are required
before merge. They are pending at this commit. Fresh guest execution is not
required for this Linux-host-only change: the Mac backend, portable runtime,
Mac build inputs and guest acceptance protocol are unchanged. The separately
implemented portable WRITE-02 half does require a fresh exact Mac artifact.

## Feature review and CI

Exact `df7b2987e8783efac23aa3d00b4e92ae1cee6893` passed independent review
and all three Woodpecker #332 workflows. An additional focused Clang
ASan/UBSan build ran all eight cases successfully. The coordinator combines
this host-only fix with the portable counterpart for a separate exact-commit
CI and Mac acceptance gate; feature success is not that integration result.

## Accepted combined integration

Both halves are accepted on main at
`e290168f0e080bd55dbbdcff65f87c4ecd2d2479` after independent review and
all three exact Woodpecker #340 workflows. Fresh System 7 run `run-nlaw0wk6`
passed all 66 records in 28.45 seconds, preserving all prior cases. The complete
fresh transcript and screenshot were inspected; normal shutdown, closed app and
disks, and slot release were verified. Archive SHA256:
`774c1d5aea25b54201d75ef86405f1ac21d4f4056275acf5796cda8ac3a9248d`.
