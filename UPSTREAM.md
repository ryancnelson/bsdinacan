# Imported upstream source

## NetBSD `yes`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `usr.bin/yes/yes.c`
- Local path: `upstream/netbsd/usr.bin/yes/yes.c`
- SHA-256: `f57930fc157302e11ea0ec67e3afe42b96c512d0f3243a2fde5425bd5c9812d8`
- Embedded RCS identifier: `$NetBSD: yes.c,v 1.9 2008/07/21 14:19:28 lukem Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. All adaptation is outside it:
the build renames `main` to `cb_yes_main`, cannedBSD headers provide the small
`sys/cdefs.h`, `stdio.h`, and `stdlib.h` surface it needs, and
`commands/yes_module.c` supplies the native-program descriptor.

`tests/test_netbsd_source.sh` pins the source hash, checks this provenance
record, proves that the separately compiled object defines the renamed entry
point, and rejects a dependency on the host's `puts`. Runtime tests consume one
exact line and directly wait for the writer, proving that closing the final pipe
reader makes the original NetBSD loop return `EXIT_FAILURE` through cannedBSD's
`EPIPE` path.

## NetBSD `strlen`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/string/strlen.c`
- Local path: `upstream/netbsd/common/lib/libc/string/strlen.c`
- SHA-256: `08969942df6b9b53bb3500e39ec47f514b876809202383088ac9d36e007d64e1`
- Embedded RCS identifier: `$NetBSD: strlen.c,v 1.3 2018/02/06 09:28:48 mrg Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. The build renames `strlen` to
the private `cb_libc_strlen` link name and archives the separate object into
`libcannedbsd.a`. The source includes `assert.h` but contains no assertion; an
import-only empty header prevents an incidental dependency on the build host's
libc without advertising an unimplemented cannedBSD assert API. A test rejects
this adaptation if the pinned source ever gains an assertion expression.

The boundary test pins the source and provenance hashes, checks the private
symbol and archive member, rejects a host `strlen` import, and requires
cannedBSD `puts` to consume the imported routine. Direct tests cover empty,
ordinary, embedded-NUL, and interior-pointer inputs.

## NetBSD `strcmp`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/string/strcmp.c`
- Local path: `upstream/netbsd/common/lib/libc/string/strcmp.c`
- SHA-256: `f06298e20a2c02e9fbe11aeb06123d8b2ad6c8d5a9a04ad68fdae2aa142524f6`
- Embedded RCS identifier: `$NetBSD: strcmp.c,v 1.4 2018/02/04 20:22:17 mrg Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

The imported file is byte-for-byte unchanged and archived separately as
`netbsd_strcmp.o`. Because it deliberately undefines the C preprocessor name
before its definition, `-Dstrcmp=cb_libc_strcmp` cannot rename it. On GCC and
Clang, the definition build asks cannedBSD's `string.h` to declare the standard
C name with the private assembler link name; application builds macro-map calls
to the private symbol so compiler builtins cannot bypass the veneer. Another
compiler will need an equivalent definition-side toolchain adapter. This is a
known porting concern, not part of the cannedBSD runtime ABI.

Hash and symbol tests reject source drift and host `strcmp`. The ordinary
bootstrap `wc.c` now uses standard `strcmp` and `strlen` spellings and its
object must import both private implementations. Direct tests cover equality,
prefix ordering, both order directions, and unsigned-byte ordering.

## NetBSD `memcpy`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Wrapper upstream/local path:
  `common/lib/libc/string/memcpy.c` /
  `upstream/netbsd/common/lib/libc/string/memcpy.c`
- Wrapper SHA-256:
  `27954650049d23535119c13fec0d333929e6ad17bb80d3c5e33ac9938f57f2a4`
- Implementation upstream/local path:
  `common/lib/libc/string/bcopy.c` /
  `upstream/netbsd/common/lib/libc/string/bcopy.c`
- Implementation SHA-256:
  `915b194678b2855a522755dad71ae4fb4f366d3f0518fd089bc6c2cb35722c44`
- Embedded RCS identifiers: `$NetBSD: memcpy.c,v 1.2 2013/12/02 21:21:33 joerg Exp $`
  and `$NetBSD: bcopy.c,v 1.13 2018/02/12 11:14:15 martin Exp $`
- License: `bcopy.c` carries the file-specific three-clause Regents of the
  University of California license retained verbatim; `memcpy.c` is the
  NetBSD wrapper that defines `MEMCOPY` and includes it.

Both inputs are byte-for-byte unchanged. The build uses `-Os` to select
NetBSD's own size-optimized byte-copy branch, appropriate for the initial
classic-machine goal, and emits the definition as `cb_libc_memcpy` through the
same GCC/Clang link-name adapter used for fortified `strcmp` builds. A future
ARM EABI port must supply NetBSD's `__strong_alias` support or an explicit
equivalent; the present x86 Linux prototype does not claim that adapter yet.

Hash, object, archive, and ordinary-source tests exclude host `memcpy`. Direct
tests cover its returned destination, zero length, offset buffers, sentinel
boundaries, NUL, and high-bit bytes. Overlap is deliberately not tested because
it is outside the `memcpy` contract.

## NetBSD `memmove`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Wrapper upstream/local path:
  `common/lib/libc/string/memmove.c` /
  `upstream/netbsd/common/lib/libc/string/memmove.c`
- Wrapper SHA-256:
  `a28ca02301f0800d67b1d8b35e1d1021b7600deb6b7e82f4179023aa22f7756b`
- Shared implementation and license: the pinned `bcopy.c` entry above.
- Embedded RCS identifier: `$NetBSD: memmove.c,v 1.2 2013/12/02 21:21:33 joerg Exp $`

The wrapper is byte-for-byte unchanged and selects `MEMMOVE` in the already
pinned implementation. It uses the same size-optimized branch, private
GCC/Clang definition link name, archive boundary, and explicitly unimplemented
future ARM EABI alias adapter as `memcpy`. Direct tests move overlapping bytes
both forward and backward; the ordinary-source object must resolve `memmove`
only to `cb_libc_memmove`.

## NetBSD `memcmp`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream/local path: `common/lib/libc/string/memcmp.c` /
  `upstream/netbsd/common/lib/libc/string/memcmp.c`
- SHA-256: `a926ba117d7a044631da27bc301769607072bdf42995e4d49dbc00139643d5ce`
- Embedded RCS identifier: `$NetBSD: memcmp.c,v 1.8 2020/01/29 09:18:26 ad Exp $`
- License: file-specific two-clause NetBSD Foundation and three-clause Regents
  licenses, both retained verbatim.

The source is byte-for-byte unchanged. A private GCC/Clang definition link name
emits `cb_libc_memcmp`; a minimal import-only `sys/types.h` supplies the standard
size and integer types without importing a host ABI. Hash, object, archive, and
ordinary-source checks exclude host `memcmp`. Direct tests cover zero length,
equality, both order directions, and unsigned high-bit bytes. Future ARM EABI
builds still require the alias support noted for the other memory routines.
