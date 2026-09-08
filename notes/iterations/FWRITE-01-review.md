# FWRITE-01-review: close the bounded acceptance gaps

Explicit coordinator repair based on frozen worker candidate
`9ab7e05277e1c61f875b4fedd37588a061dcbf63`, branch
`work/FWRITE-01-review`. The original worker branch is preserved. The reviewed
fwrite runtime, ABI, headers, error mapping and existing binary capture wrapper
are unchanged by this repair. No core or output-helper change was needed.

## Falsifiable scope

The original tests counted writes for zero requests, used only a full outer API
table and full returned storage, and ran only the binary happy path on Mac.
They did not establish the documented INT64 request boundary. The previously
reported return 90 from invalid test setup is not accepted as either required
semantic negative control here.

A new compact shared `fwritecompat` entry runs the ordinary private source in
both Linux and the Mac fixture. It checks:

- Zero size/count with unusable buffer and stream pointers: counted write,
  read, state, errno getter/setter/location, and input-state callbacks all remain
  untouched. The ordinary zero-only branch makes no errno/status call itself.
- Actual allocated outer API storage ending before the output accessor; a NULL
  accessor; a NULL result; a wrong returned version; and actual allocated state
  ending before stderr_error. Rejection produces ENOSYS before a write, preserves
  both flags and short storage, and restores the valid binding before release.
- Actual outer storage ending before the input accessor, plus a full table with
  only that accessor NULL: fwrite still works. Each rejected binding is followed
  immediately by success with the known valid state without resetting its flags.
- A state accessor that changes errno, as well as successful write callbacks
  that do so: successful fwrite preserves the incoming sentinel.
- Exact 3+5 writes for two four-byte elements, checking requested remainders,
  pointer offsets, bytes and counts. Six bytes then EPIPE returns one element;
  a later two-byte write succeeds while preserving the sticky error and EPIPE.
  The combined emitted bytes are exactly `abcdefij`, without replaying fragments.
- SIZE_MAX multiplication overflow, NULL/invalid streams and buffers, flags
  unchanged on argument errors, an actual ILP32-safe wide positive over-return
  of 4294967304, and the min(SIZE_MAX, INT64_MAX) callback request. The huge-count
  adapter returns zero without reading the tiny supplied buffer; it does not
  pretend to allocate or transfer enormous memory.

The original native failure, stream-separation and sticky tests remain. Their
recovery branch now restores the binding on early failure as well as success.
The real two-pipe binary wrapper still independently checks NUL/0xff stdout bytes
and distinct stderr bytes. The private-source fence now checks errno/strcmp as
well as fwrite/ferror and rejects host stream identities. The ordinary source is
also included in static analysis.

The base's 59 existing Mac records and original fwrite wrapper record are
preserved. The added compatibility record makes **61 PASS records plus ALL
PASS** on this isolated feature. Checked registration is present in both shared
native and Mac fixtures. The fixed 64-program limit and general fixture are
unchanged. Integration must retain other independently added records.

## Evidence

Disposable source archive before this note, SHA-256:
`01297f0137e9446e5a8e7f65fc71bcb13e59a0f9993facdbae0f1df47152826b`.
Environment: `tribblix-woodpecker-agent:3.18.0`, Alpine 3.22.5, GCC 14.2,
Clang 20.1.8, disposable Linux container with network disabled.

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --fwrite
```

The first focused build/run of the repair passed, exit 0. Runtime source SHA-256
is unchanged from the worker candidate:
`e75df0c1e6c2dd794f5624ef0d9cd2ec007433d5c2fb25be88550fd1c503a321`.

Two separate disposable runtime copies run the same focused command. These are
after-implementation regression controls, not preimplementation TDD evidence:

1. Stop after the first positive fwrite callback. Expected semantic failure is
   the exact 3+5 split assertion: `command fwritecompat returned 60`, followed
   by `FAIL: fwrite fwrite probe exit`, exit 1. Altered libc SHA-256:
   `c08fc1b160603a592a9016a8ebe3d431a62a77b66ac79cbd300ba3c6b4371ad6`.
2. Suppress fwrite's selected-stream error marking on negative/zero/over-return.
   The partial-error/recovery assertion fails: `command fwritecompat returned
   61`, followed by `FAIL: fwrite fwrite probe exit`, exit 1. Altered libc SHA-256:
   `3d8845f7883eab3ed570c28f083229e1fb5f3749edf4826de84638f8c78f44aa`.

`python3 -B tests/test_mac_guest.py`: 18 tests passed.
Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed, including
native/shared cases, ASAN/UBSAN, private-source fences, publication, protocol,
clean build-mode isolation and GCC static analysis.
Exact feature Woodpecker and coordinator-owned exact-artifact Mac execution:
pending. No guest acceptance or timing is claimed by this repair worker.
