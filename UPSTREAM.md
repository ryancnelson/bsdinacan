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

## NetBSD `strchr`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream/local path: `common/lib/libc/string/strchr.c` /
  `upstream/netbsd/common/lib/libc/string/strchr.c`
- SHA-256: `ebe71501c3aa96b35445642eeb72ab6c73f0fa561ce83b9f78d4c0e06c155cb9`
- Embedded RCS identifier: `$NetBSD: strchr.c,v 1.7 2020/04/07 08:07:58 skrll Exp $`
- License: file-specific three-clause Regents license, retained verbatim.

The source is byte-for-byte unchanged and builds as `cb_libc_strchr`. A minimal
import-only `namespace.h` avoids pulling NetBSD's complete internal namespace
into cannedBSD; the upstream `index` alias is not part of the advertised libc
surface. Direct and ordinary-source tests cover first match, later match,
terminal NUL, absence, and conversion of the search `int` to `char`.

## NetBSD `dirname`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream/local path: `lib/libc/gen/dirname.c` /
  `upstream/netbsd/lib/libc/gen/dirname.c`
- SHA-256: `05ad1f66a7a5a4ceee33fe767a3410c60aa76e4ed670b5d57ab9198a0a2a892b`
- Embedded RCS identifier: `$NetBSD: dirname.c,v 1.14 2018/09/27 00:45:34 kre Exp $`
- License: file-specific two-clause NetBSD Foundation license, retained
  verbatim -- distinct from the three-clause Regents license on every
  other pinned import in this file.

The imported file is byte-for-byte unchanged and builds as
`cb_libc_dirname_upstream` (a plain `-D` link-name rename, like `strlen`;
confirmed by reading the file that no `#undef`/macro games would defeat
it -- the only other occurrence of the literal text `dirname` is inside
an `__weak_alias(dirname,_dirname)` line gated on a macro this project's
minimal `namespace.h` shim never defines). Three new minimal import-only
shims were added for its includes: `compat/netbsd/include/sys/param.h`
(only the `MIN` macro), `compat/netbsd/include/limits.h` (`PATH_MAX`,
defined from the pre-existing `CB_PATH_MAX` rather than a second pinned
constant), and `compat/netbsd/include/libgen.h` (empty -- the file
includes `<libgen.h>` only for its own prototype's self-consistency).

The imported `dirname()` itself returns a pointer into a function-local
static buffer shared process-wide across every cooperatively scheduled
task -- safe only by single-task convention on a traditional Unix
process. The `cb_libc_dirname` veneer (`libc/cb_libc.c`) copies that
result into a new, genuinely task-owned buffer (`struct cb_task
.dirname_buffer`, reached via a new append-only `cb_api_v1` accessor,
`dirname_buffer_location`) before ever returning to the caller, so one
task's call can never be silently overwritten by another's. The boundary
test pins the source and provenance hashes; direct tests cover the
NetBSD-documented pathname edge cases (`NULL`, empty, no slash, all
slashes, trailing slashes, an exact-fit boundary length), same-task
repeated calls, cross-task isolation under forced interleaving, and
`ENOSYS` on an old or field-absent runtime table.

## NetBSD `basename`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream/local path: `lib/libc/gen/basename.c` /
  `upstream/netbsd/lib/libc/gen/basename.c`
- SHA-256: `f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87`
- Embedded RCS identifier: `$NetBSD: basename.c,v 1.12 2023/01/18 08:07:22 simonb Exp $`
- License: file-specific two-clause NetBSD Foundation license, retained
  verbatim -- distinct from the three-clause Regents license on every
  other pinned import in this file (matching `dirname`'s own license).

The imported file is byte-for-byte unchanged and builds as
`cb_libc_basename_upstream` (a plain `-D` link-name rename, like
`dirname`; confirmed by reading the file that no `#undef`/macro games
would defeat it -- the only other occurrence of the literal text
`basename` is inside an `__weak_alias(basename,_basename)` line gated on
a macro this project's minimal `namespace.h` shim never defines). Reuses
the three existing import-only shims `dirname` already needed
(`compat/netbsd/include/sys/param.h`, `limits.h`, `libgen.h`) -- no new
shims required.

The imported `basename()` itself returns a pointer into a function-local
static buffer shared process-wide across every cooperatively scheduled
task, exactly like `dirname()`. The `cb_libc_basename` veneer
(`libc/cb_libc.c`) copies that result into a new, genuinely task-owned
buffer (`struct cb_task.basename_buffer`, reached via a new append-only
`cb_api_v1` accessor, `basename_buffer_location`, appended after
`closedir` -- the actual current tail) before ever returning to the
caller. This buffer is deliberately separate from `dirname`'s: reusing
it would let a `basename()` call silently invalidate an already-returned
`dirname()` result in the same task, coupling two otherwise independent
pinned imports for no reason. The boundary test pins the source and
provenance hashes; direct tests cover the same NetBSD-documented
pathname edge cases `dirname` covers, same-task repeated calls,
cross-task isolation under forced interleaving, retained `dirname`
results alongside a `basename` call in the same task, and `ENOSYS` on an
old or field-absent runtime table.

## NetBSD `basename` (command)

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `usr.bin/basename/basename.c`
- Local path: `upstream/netbsd/usr.bin/basename/basename.c`
- SHA-256: `717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd`
- Embedded RCS identifier: `$NetBSD: basename.c,v 1.16 2019/02/01 08:29:04 mrg Exp $`
- License: file-specific three-clause Regents of the University of California license, retained verbatim.

The imported file is byte-for-byte unchanged. The build renames `main`
to `cb_basename_main`. It depends on a C-only locale boundary
(`setlocale`), a libc `basename` implementation, and (unlike `dirname`'s
command) `strlen`/`strcmp` for its optional suffix-stripping argument --
it accepts one path and an optional suffix, strips the suffix only when
it is strictly shorter than the result and matches its end, and writes a
NUL into the returned buffer at the truncation point, so the returned
string must be genuinely writable (satisfied by `cb_libc_basename`'s
task-owned buffer, not an immutable string). For an empty path it prints
just a newline and exits `0` *before* ever calling libc `basename`,
unlike libc `basename("")`, which returns `"."`.

## NetBSD `printenv`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `usr.bin/printenv/printenv.c`
- Local path: `upstream/netbsd/usr.bin/printenv/printenv.c`
- SHA-256: `d355c07fc5a351d38e2f8552899b456f1300a61408ebf2e2af47c5f52de974db`
- Embedded RCS identifier: `$NetBSD: printenv.c,v 1.12 2011/09/06 18:26:55 joerg Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. It is the first pinned command
to require the full dependency ladder built across `PENV-01`
(task-local `environ`), `PENV-02` (`exit`/`__dead`), `PENV-03` (empty-option
`getopt`), `PENV-04` (bounded `printf`/`fprintf`), and `PENV-05` (`errx`); no
new libc surface was added to make it compile. The build renames `main` to
`cb_printenv_main`, cannedBSD headers supply the `sys/cdefs.h`, `stdlib.h`,
`stdio.h`, `string.h`, `unistd.h`, and `err.h` surface it uses, and
`commands/printenv_module.c` supplies the native-program descriptor,
registered alongside `wc` and `yes` in `cb_register_base_programs`. A new,
minimal import-only `libc/include/sys/types.h` (transitively pulling in
`stddef.h`/`stdint.h`) satisfies the source's `#include <sys/types.h>`; no
type from it is otherwise exercised.

One narrow, documented toolchain concession: the source's local
`extern char **environ;` redeclaration inside `main` (a redundant safety net
common in older BSD code, harmless on a system where `environ` is a real
extern variable) expands, once `environ` becomes the macro
`(*cb_libc_environ_location())` needed for task-local semantics, into a
declarator that both GCC and Clang parse as an unprototyped
redeclaration of `cb_libc_environ_location` itself — triggering
`-Wstrict-prototypes`, which is part of both compilers' `-Wall` for C and
therefore fatal under this project's `-Werror`. `-Wno-strict-prototypes` is
added to this one file's compile rule only; every other translation unit
keeps full `-Wstrict-prototypes` coverage. This is a known, narrow,
compiler-observable consequence of the task-local `environ` design
(`PENV-01`), not a defect in the pinned source and not a broadening of the
runtime ABI — the same category of documented toolchain adapter already
recorded above for `strcmp`/`memcpy`/`memmove`.

`tests/test_netbsd_source.sh` pins the source hash and provenance record
alongside `yes`, checks the separately compiled object defines the renamed
entry point, and rejects host-symbol imports for `environ`, `getopt`, `errx`,
`exit`, and `memcmp` in favor of their private cannedBSD counterparts.
`tests/test_printenv_behavior.sh` exercises named, missing, empty-valued, and
enumerate-all lookups, the `=`-in-name diagnostic, the too-many-arguments
usage diagnostic, pipeline use, and output redirection, entirely through the
shell against the real registered command — no direct unit-level probe was
needed since every required behavior is externally observable this way.

## MoreFiles read-only catalog feasibility inputs (REUSE-02)

Official MacPerl CVS snapshot: `https://sourceforge.net/code-snapshots/cvs/m/ma/macperl.zip`
(archive SHA-256 `afc00b6009d79d37b7146116e6ce1a44a4f530303b693cb217c9de0604bd6222`).
All files below are CVS revision 1.1 under
`perl/macos/ext/Mac/MoreFiles/MoreFilesSrc/`; local directory is
`upstream/morefiles/`. Each is byte-for-byte unchanged, including its notice.
`pins.json` is the machine-readable ledger checked by the diagnostic.

| File | SHA-256 |
| --- | --- |
| `IterateDirectory.c` | `0e2cd7812068d0d2b95d3dc023a2e10114504e8183302ea178c742d4359de882` |
| `IterateDirectory.h` | `6746204bebd5d25c1dabda3d1f4693c67628b7490333fe8346fd12d82dd7ac31` |
| `MoreFilesExtras.c` | `56d03290e6ac1912c8c4eb7b7945b5f154a244fe17a9b01982502bc94d920865` |
| `MoreFilesExtras.h` | `41d0ddebd6aa650083e22f0cf023b3092accc36d890de00d889dac9b60995775` |
| `MoreFiles.h` | `51987f9a1a4542a86d0f1fe9a192a9ef5c53a5232cf008324d64dcfa0246600b` |
| `MoreDesktopMgr.h` | `ac811ae307b612bf40a16fb30b91eb8e732f4ef96314ba81d854756c1623d998` |
| `FSpCompat.h` | `66fdc2c0f457f42cfd964f73c827ce34f2c83fe83634353fcd966b713e5795d4` |
| `Optimization.h` | `19ec510e046f2291905d8b41bcc12da0c2c920ad45620eb813181e2f3f9c4800` |
| `OptimizationEnd.h` | `587661dded9e22e941555dbed289e83ead4348c3a1f9a4692b32872e0c4234c7` |

`IterateDirectory.c` and `.h` retain the Jim Luther/Apple 1995–1999
permission to incorporate without restriction, as-is and at the user’s
responsibility. The other seven files retain their Apple 1992–1999
sample-code notices, including the requirement that altered redistributed
source identify its Apple origin and changes. No source alteration or
replacement license is applied here. These files are diagnostic inputs only,
not linked into cannedBSD. The pinned SDK cannot currently compile them; see
`notes/iterations/REUSE-02.md`.

## MoreFiles owned catalog derivative (REUSE-03)

`platform/mac68k/probes/catalog/catalog.c` is explicitly altered from
revision-1.1 `IterateDirectory.c` and `MoreFilesExtras.c`, not another unchanged
import. Both original hashes/notices remain in the MoreFiles ledger above.
The derivative retains Jim Luther/Apple and Apple sample-code notices and
identifies extraction/changes. Its `PROVENANCE.md` records exact source
functions/lines, the independently pinned directory-bit constant, deliberate
error-handling differences, and the existing SDK types it uses. No imported
source byte or cannedBSD runtime ABI changed. The GPL-compatible host fixture
builder uses only the pinned toolchain image's libhfs; it is not a Mac runtime
dependency and its linked binary is excluded from the artifacts.

REUSE-03's host-only fixture generator also includes an explicitly attributed
GPL-2.0-or-later adaptation of cdrtools `mkisofs/desktop.c` 1.10 (James Pearson /
J. Schilling). `platform/mac68k/probes/catalog/PROVENANCE.md` pins the exact
source revision and hash. It builds empty Desktop Manager metadata for the
protected HFS fixture; this code is not linked into the Mac probe or runtime.

## NetBSD `dirname`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `usr.bin/dirname/dirname.c`
- Local path: `upstream/netbsd/usr.bin/dirname/dirname.c`
- SHA-256: `839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`
- Embedded RCS identifier: `$NetBSD: dirname.c,v 1.12 2011/09/16 15:39:25 joerg Exp $`
- License: file-specific three-clause Regents of the University of California license, retained verbatim.

The imported file is byte-for-byte unchanged. The build renames `main` to `cb_dirname_main`. It depends on a C-only locale boundary (`setlocale`) and a libc `dirname` implementation.
