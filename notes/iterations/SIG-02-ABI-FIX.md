# SIG-02-ABI-FIX — preserve rename with older API tables

Base: `7712111f387fbf83f4deedb1d9d873e130185ad5`, branch
`work/SIG-02-ABI-FIX`. This bounded follow-up preserves the authored SIG-02
candidate and repairs the ABI regression found during independent review.

Appending `set_interrupt` enlarged `struct cb_api_v1`, causing the existing
whole-structure size check in `cb_libc_rename` to reject an otherwise valid
older table containing `rename`. The guard now checks the end of the `rename`
field itself. No API fields move or change, and signal behavior is unchanged.

The portable SIG-02 probe now allocates only the prefix through `rename`,
creates a file, calls ordinary private-header `rename`, and verifies the moved
byte and the source's ENOENT result. The allocation physically excludes the
new interrupt callback. The existing Linux and Mac interrupt test wiring runs
this regression; the Makefile also tracks its added private stdio header.

## Actual red and green

In an isolated source export using the existing pinned Linux toolchain image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
with container networking disabled:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --signal-libc
```

With the regression added and the original guard unchanged, compilation
succeeded and the probe failed with `signal libc failed status 24`, exit 1.
After changing only the guard, the same build and probe succeeded with
`signal libc tests passed`, exit 0. `./build/test_core --signals` also exited 0.
An initial container-entrypoint invocation error was corrected before running
the behavioral red and is not counted as test evidence.

## Qualification

The full Linux gate and exact-commit Woodpecker workflows are pending.
Mac and Solaris guest acceptance remain coordinator-owned pending gates;
this worker did not operate either guest. Integration with LS-02 must still
preserve the accepted ABI append order and rebuild the combined result.

Shared production symbol changed: `cb_libc_rename`. No imported source changed.
