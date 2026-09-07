# Solaris 9 SPARC portability gate

The second host target is Solaris 9 on a 32-bit SPARCstation 5 (`sun4m`).
Run cannedBSD as a native SPARC executable inside the guest. QEMU supplies
only the test machine; cannedBSD still owns its internal tasks, descriptors,
pipes, and RAMFS.

## Build

With GCC and GNU make installed in the guest, run:

```sh
/bin/ksh tools/solaris9-build.sh
```

The script selects `HOST=solaris9`, performs a clean build, runs the shared
runtime tests and acceptance pipelines, and reports the executable format.
Linux retains `make ci`, including `/proc`, sanitizer, source provenance,
architecture, and GCC analyzer checks. Those host-side checks are not claimed
as Solaris guest tests.

The Solaris configuration is for SunOS 5.9 SPARC, not Solaris 10 or illumos.
`src/host_posix.c` shares console I/O and context lifecycle with Linux, with
these Solaris 9 adaptations:

- The original `makecontext` interface receives the high stack address,
  `base + size - 8`. Solaris 10 changed this convention. See
  [Oracle's compatibility notes](https://docs.oracle.com/cd/E19253-01/816-5168/swapcontext-3c/index.html).
- Monotonic time uses `gethrtime`; the build links `librt` for POSIX clocks
  and sleeping.
- A private `stdint.h` adapter includes Solaris's `inttypes.h` and supplies
  the missing `SIZE_MAX` constant.
- GCC 3.x uses `-pedantic` rather than the newer `-Wpedantic` spelling.
- GNU C99 mode is required for GCC 3.4.6 to expose the original Solaris
  headers' 64-bit integer types. Strict `-std=c99` fails in those headers.
- The runtime gate uses `/bin/ksh`; the original `/bin/sh` cannot parse its
  command substitutions. Overflow probes account for 32-bit `size_t` without
  attempting a multi-gigabyte allocation.

## Test machine preparation

Reuse the Solaris 9 SS-5 runner and overlay preparation from the
[qemu-sun4v-illumos project](https://github.com/ryancnelson/qemu-sun4v-illumos)
and its associated Solaris 9 workbench. Create a separate overlay for every
writable disk and a private NVRAM copy. Keep the QEMU process under persistent
terminal ownership and use its Unix serial and monitor sockets.

The stock guest has development headers, assembler, and linker, but no GCC.
The toolchain being qualified is the Sunfreeware Solaris 9 SPARC GCC 3.4.6,
GNU make 3.81, and libiconv 1.8 packages from the
[DEU archive mirror](https://ftp.deu.edu.tr/pub/Solaris/sunfreeware/sparc/9/).
Downloaded compressed package SHA-256 values:

| Package | SHA-256 |
|---|---|
| `gcc-3.4.6-sol9-sparc-local.gz` | `44012fe5ee016947c62e888e659c4c94b41b95d4b284896d78b1b446a99890e4` |
| `make-3.81-sol9-sparc-local.gz` | `c19c19ec433e91ca175c79418e80d1cbb57edf220feb423c87f3100f6b288140` |
| `libiconv-1.8-sol9-sparc-local.gz` | `ab58bfa16aabb35c5a9c897138681850c27d34760705b5bf70205974a09ffe65` |

These hashes identify the downloaded artifacts, not an independently verified
publisher signature. Carry decompressed package datastreams and a source tar
archive on read-only HSFS media. On a Tribblix QEMU host the file block backend
needs `locking=off`, including for inserted CD images.

Set `TMPDIR` to a directory on a disk with enough free space before `pkgadd`.
The stock root partition has only about 100 MiB free; GCC unpacking exhausts
its default `/var/tmp`. The test uses `/usr/local/pkg-tmp` on the larger `/usr`
filesystem. Keep installation and build logs outside the source tree.

## Qualification status

On 2026-09-07, `/bin/ksh tools/solaris9-build.sh` passed inside SunOS 5.9
`sun4m`, using GCC 3.4.6, GNU make 3.81, and the system assembler/archiver.
The clean build used the checked-in configuration without CFLAGS overrides.
Its final output included:

```text
all core tests passed
launcher test passed
build/bsdinacan: ELF 32-bit MSB executable SPARC Version 1, dynamically linked, not stripped
SOLARIS9_CANNEDBSD_TEST=PASS
```

The runtime gate asserts exact `HELLO` output for the shell pipeline and `5`
for the libc-backed `wc` pipeline. The core suite also exercises the NetBSD
`yes`/string/memory imports, descriptor and allocation failures, path traversal,
cooperative scheduling, and cleanup. A separate context canary confirmed that
the original Solaris 9 high-stack convention resumes successfully; supplying
the Linux base-stack convention crashed the canary.

The VM clock lagged the build host. Extracted source timestamps were normalized
to the guest clock before the clean build; the shared host clock was not changed.
The complete Linux `make ci` gate also passed after the shared-backend changes.
No sanitizer, Linux `/proc` check, or automatic hosted Solaris CI is claimed for
this guest. Physical SPARC hardware and classic Mac remain unqualified.
