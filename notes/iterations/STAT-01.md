# STAT-01 — default file creation mode

Base: `3e02a2cb78edd9732b50ef3c123ab86d7ddda0bd`.
Branch: `work/STAT-01`. Coordinator-assigned scope: the private header constant
needed by pinned NetBSD tee, without importing an operating-system header.

## Contract and source

`libc/include/sys/stat.h` now exposes only the integer macro
`DEFFILEMODE` (`0666`). It adds no `stat`, `fstat`, `chmod`, `umask`, `mode_t`,
or permission enforcement. Existing variadic `open` consumes an `int`; the
RAMFS records that mode as metadata. Permission enforcement remains deferred
under SPEC section 7.

The reference is NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`,
[`sys/sys/stat.h`](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/sys/sys/stat.h),
line 193: six owner/group/other read/write bits, with the immediately preceding
comment identifying octal 0666. Reference file SHA256:
`19f5223d4ef14589810543f443a1d4fcd06aefdfee5416bd867137ad5cb4431c`.
Its file notice is the three-clause Regents license with the retained
USL/AT&T provenance statement. The downloaded reference is not vendored;
this small cannedBSD-owned header spells the numeric compatibility constant,
without copying the full header, layouts, declarations, or implementation.

## Observable fixture and boundary

The existing ordinary `libc_file_probe.c` includes private `sys/stat.h`,
`fcntl.h`, and `unistd.h`, creates a regular file using `DEFFILEMODE`, and
closes it. Before any later task teardown, its shared runtime-side wrapper
requires the closed descriptor to report `EBADF`, pathname `stat` to report
mode **0666**, regular type and size zero, and a reopened descriptor's `fstat`
to report the same metadata. It closes that descriptor, proves `EBADF`,
unlinks the file, and requires subsequent pathname `stat` to report `ENOENT`.
The asserted mode is an independent literal, not the macro under test.

This extends the existing Linux/Mac `fileprobe` case. No program registration,
ABI, libc runtime, tee source, signal behavior, global capacity, or transcript
record count changes: all **65** records on this branch's base remain.
The compiler's dependency output must name the private `sys/stat.h`; the
ordinary-source fence rejects runtime names and host `open`/`close` imports,
and requires the prefixed libc symbols. Header edits are explicit Make
prerequisites, so the wrong-value control must recompile the ordinary object.

## Verification

Linux evidence uses the pinned `tribblix-woodpecker-agent:3.18.0` image,
Alpine 3.22.5, GCC 14.2, with `--network none --entrypoint /bin/sh` and an
isolated source export mounted at `/work`. Focused command:

```sh
make LDLIBS=-lucontext build/test_core
build/test_core --file
```

The exact clean base passed. Before adding the private header, this actual
ordinary-source compilation failed with `DEFFILEMODE undeclared`, exit 1:

```sh
cc -Iinclude -Ilibc/include -std=c11 -Dmain=cb_file_probe_main \
  -c tests/libc_file_probe.c -o /tmp/stat-before.o
```

That is missing-surface feasibility evidence, **not behavioral TDD red**;
without our header the host header was reachable and did not provide the
required constant. With the private header, the focused test passes.

A separate after-implementation control changes only the exported private
header from `0666` to `0600`, rebuilds, and runs the same focused test:

```sh
cp libc/include/sys/stat.h /tmp/stat-good.h
sed -i s/0666/0600/ libc/include/sys/stat.h
make LDLIBS=-lucontext build/test_core
build/test_core --file
cp /tmp/stat-good.h libc/include/sys/stat.h
make LDLIBS=-lucontext build/test_core
build/test_core --file
```

 The
observed failure was shared `fileprobe` status 53 (actual stored-mode assertion),
executable exit 1, without running a source/hash gate. Restoring the header
restored focused success (exit 0).
Header SHA256 after restoration:
`d44cb98a946566fffa2e81b559189ce7734814189de0df0caa639d85ec0e724c`;
wrong-value header SHA256:
`f86d7933aca155fe51c8ce4a593f30938204a2ebf842a4d83a2e8384675aa7a8`.

Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed (exit 0), including
normal and sanitizer suites, source fences, build-mode isolation, publication
checks, and GCC analyzer. Final local publication/diff checks also passed.
Immutable Woodpecker results are recorded at handoff; exact Mac artifact
execution belongs to the coordinator and is not yet claimed here.

## Coordinator integration acceptance

Combined integration `f1a769a97017add822270526e06f826a1ab6bde4` passed independent
review and exact Woodpecker #356 ci, mac68k and mac-automation. Fresh guest
`run-nszacm8k` passed all 66 current records, including the expanded fileprobe;
all prior records were retained. Archive SHA256:
`9148d9b45018bd23e6a461de6b58f6ccd342b3412990e3e72f71230f9edec46f`.
The full transcript and decoded screenshot were inspected. Receipt confirms
application and guest disks closed normally; driver confirms slot release,
34.06 seconds from cold start. Solaris qualification is pending SOLARIS-01
integration under the transition policy; no native Solaris success is claimed.
