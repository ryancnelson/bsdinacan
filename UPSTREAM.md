# Imported upstream source

## Standing constraint: compiler idiom recognition on link-name-bound imports

Any imported source whose function is bound to its private link name via
the `__asm__("cb_libc_X")` declaration trick (as opposed to a plain
preprocessor `#define X cb_libc_X`, which renames the source-level
identifier itself before the compiler ever sees the standard library
name) keeps its *source-level* name as the standard, compiler-recognized
one — `memset`, `memcpy`, `memmove`, `memcmp`, `strcpy`, `strcmp` today.
GCC's loop-idiom recognition (`-ftree-loop-distribute-patterns`, on by
default from `-O2`) can rewrite a loop inside such a function's own body
into a call back to that same standard name if the loop shape matches a
known idiom (a constant-fill loop matching `memset` is the textbook
case). Because the `__asm__` binding is in effect for the whole
translation unit, that compiler-generated call resolves back to the
function currently being compiled, producing unbounded self-recursion —
a stack-overflow segfault at runtime, with no compile-time symptom.
`memset.c` hit this exactly (see its entry below); fixed there with
`-fno-builtin-memset` plus a GCC-only `-fno-tree-loop-distribute-patterns`
(Clang rejects that flag outright, so it is conditional on `$(CC)` in the
Makefile).

**Checked empirically, not by analogy, whether the same risk is live in
every other already-merged link-name-bound import** (`strcpy`, `strcmp`,
`memcpy`, `memmove`, `memcmp`): compiled each with its current, unmodified
Makefile flags and disassembled the result. None contain any call
instruction at all — every one compiles to fully straight-line code, so
the idiom-recognition rewrite never fires for any of their loop shapes.
This is a property of those specific loops (byte-at-a-time copy/compare
with a NUL or count termination condition, not a plain constant-fill),
not a guarantee that would survive a future GCC version, a changed
optimization level, or a future edit to any of these files. **No flags
were added to their build rules**, since AGENTS.md's rule against libc
surface unsupported by a measured diagnostic applies here too: adding
`-fno-builtin-*` to functions with no measured self-recursion would be
exactly the kind of unearned, speculative change this project avoids.

**Same empirical measurement performed for Retro68 m68k at `-Os`** (under
`MAC68K-CMD-01`): `cb_memset` was given `-fno-builtin-memset` and
`-fno-tree-loop-distribute-patterns` (guarded via CMake `check_c_compiler_flag`).
All six m68k objects (`memset`, `memcmp`, `memcpy`, `memmove`, `strcpy`, `strcmp`)
were disassembled with `m68k-apple-macos-objdump -d` and inspected for subroutine
calls (`bsr`/`jsr` or recursive symbol references). All six compile to pure
straight-line and local branch loops (`bras`, `bnes`, `beqs`, `blss`, `bcss`, `rts`)
with zero external calls or self-calls. None of `memcmp`, `memcpy`, `memmove`,
`strcpy`, `strcmp` required extra flags on m68k.

**Recipe for any future import binding a new function to a compiler-
recognized standard name via this `__asm__` trick:** disassemble the
compiled object and grep for a call/branch-and-link instruction targeting
either the function's own private symbol or the plain standard name
before considering the import safe at whatever optimization level the
Makefile actually uses. Do not infer safety from an import compiling and
its own direct unit tests passing — the recursion is only reachable
through GCC's own idiom-matching, which is independent of what the
imported source or the tests do.

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

## NetBSD `echo`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/echo/echo.c`
- Local path: `upstream/netbsd/bin/echo/echo.c`
- SHA-256: `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04`
- Embedded RCS identifier: `$NetBSD: echo.c,v 1.23 2021/11/16 21:38:29 rillig Exp $`
- License: file-specific three-clause Regents of the University of California license, retained verbatim.

The imported file is byte-for-byte unchanged, matching the hash already
measured in `notes/iterations/utility-roadmap-20260908.md`. It is compiled
as its own command object (`netbsd_echo.o`, private link name
`cb_netbsdecho_main`) once `PROGNAME-01` and `STDOUT-01` landed on `main`
and were merged into this branch, exactly as `dirname`/`basename`/`yes`
already do: `setprogname`, `setlocale`, `strcmp`, `printf`, `putchar`,
`fflush`, `ferror`, and `err` all resolve to this project's private
`cb_libc_*` veneers, never the host's. It registers under the distinct
command name `netbsdecho`, per the design's item 3, rather than replacing
the existing shell builtin `echo`. See `notes/iterations/ECHO-01.md` for
the full integration record, including the executable exact-output/status
test matrix and the write-failure/task-isolation coverage.

The pinned source's `main` never reads `argc` (marked `/* ARGSUSED */`,
a lint-only annotation with no effect on GCC/Clang warnings), so its
build rule alone carries `-Wno-unused-parameter`; no other imported
source loses that coverage.

## NetBSD `strcpy`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/string/strcpy.c`
- Local path: `upstream/netbsd/common/lib/libc/string/strcpy.c`
- SHA-256: `36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad`
- Embedded RCS identifier: `$NetBSD: strcpy.c,v 1.4 2018/02/04 20:22:17 mrg Exp $`
- License: file-specific three-clause Regents of the University of California license, retained verbatim.

The imported file is byte-for-byte unchanged. It uses the `CANNEDBSD_BUILDING_LIBC_STRCPY` definition to emit a compiler-specific assembly renaming macro for the link name since the source contains an `#undef strcpy`.

## NetBSD `strtoimax`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/stdlib/strtoimax.c`
- Local path: `upstream/netbsd/common/lib/libc/stdlib/strtoimax.c`
- SHA-256: `c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5`
- Embedded RCS identifier: `$NetBSD: strtoimax.c,v 1.2 2013/12/02 12:20:44 joerg Exp $`
- License: two-clause (Copyright (c) 2005 The DragonFly Project; Copyright
  (c) 2003 Citrus Project), retained verbatim in the imported file. This
  is a *different* license from `_strtol.h` below, imported alongside it.

## NetBSD `_strtol.h` (shared template body, imported for `strtoimax` only)

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/stdlib/_strtol.h`
- Local path: `upstream/netbsd/common/lib/libc/stdlib/_strtol.h`
- SHA-256: `f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c`
- Embedded RCS identifier: `$NetBSD: _strtol.h,v 1.11 2017/07/06 21:08:44 joerg Exp $`
- License: file-specific three-clause Regents of the University of
  California license (1990, 1993), retained verbatim -- distinct from
  `strtoimax.c`'s own license above.

Both files are byte-for-byte unchanged. `strtoimax.c` is a thin driver
(`#define _FUNCNAME strtoimax`, `__INT intmax_t`, `__INT_MIN INTMAX_MIN`,
`__INT_MAX INTMAX_MAX`, then `#include "_strtol.h"`); `_strtol.h` is the
actual generic base-2..36 parsing loop shared with `strtol`/`strtoll`
(neither of which this project imports -- the shared template only emits
code for the `_FUNCNAME` the including file defines). Compiled with
`-DHAVE_NBTOOL_CONFIG_H=1`, NetBSD's own established mode for building
this template without its full locale subsystem: selects the plain
`intmax_t strtoimax(const char *, char **, int)` signature using plain
`isspace()`, while still setting ordinary userland `errno` (`ERANGE` on
overflow/underflow, `EINVAL` on an invalid base) rather than the
`_KERNEL`/`_STANDALONE` `panic()` path -- verified directly against this
file's own `#ifdef` structure, not assumed. Requires the private,
import-only `compat/netbsd/include/nbtool_config.h` (empty; satisfies
`strtoimax.c`'s own conditional `#include` line and nothing else) and
`compat/netbsd/include/assert.h`'s `_DIAGASSERT(e)` no-op (used once, by
`_strtol.h`, for a non-`NULL` argument check -- never a host `assert()`).
Renamed to the private link name `cb_libc_strtoimax` via
`-Dstrtoimax=cb_libc_strtoimax`; unlike `strcmp`/`memcpy`/`strcpy`, this
file never does its own `#undef strtoimax`, so the plain rename is not
cancelled and needs no assembler-name link adapter. `intmax_t`/
`INTMAX_MAX`/`INTMAX_MIN` are borrowed from the host's own `<stdint.h>`
(a pure compile-time type/constant definition with no callable surface,
covered by this project's existing fundamental-header exemption); the
actual host-leak risk this task closes is the `strtoimax` *function*
declaration itself, which `libc/include/inttypes.h` always maps to the
private veneer, never left to resolve against any host declaration.

`isdigit`/`isspace` (`libc/include/ctype.h`) are cannedBSD-owned, not
imported: NetBSD's real implementations are table-driven through the
same rune-locale machinery `setlocale`/`LOCALE-01` deliberately does not
import wholesale, for a dependency this task only needs in the plain
C-locale ASCII sense (traced directly from the pinned `head.c`'s own
`isdigit` call, and from `_strtol.h`'s internal `isspace` call for
leading-whitespace skipping).

## NetBSD `head`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream/local path: `usr.bin/head/head.c` / `upstream/netbsd/usr.bin/head/head.c`
- SHA-256: `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`
- Embedded RCS revision: head.c 1.24, 2016-05-12.
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim.

Source bytes are unchanged. Both builds rename main to cb_head_main and use
cannedBSD private headers. commands/head_module.c requests a per-command128KiB
stack for the source's65536-byte automatic buffer. The ordinary imported source
never includes runtime-private headers. The portable headprobe verifies separate
RAMFS output/error files internally, including the65538-byte boundary, binary
bytes and checked pipe producer/consumer statuses. Source/symbol fences pin the
import and reject unprefixed libc dependencies. Compiler stack-usage reports
are isolated function-frame evidence, not a claim of a measured peak call chain.

## NetBSD `rm`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/rm/rm.c`
- Local path: `upstream/netbsd/bin/rm/rm.c`
- SHA-256: `aebdd0b46b263ca8d5c6b12cc5bf27a21b878c5ad8230c6248583b43ace53f22`
- Embedded RCS identifier: `$NetBSD: rm.c,v 1.58 2026/04/26 01:49:28 jschauma Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1990, 1993, 1994, 2003), retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. Registered via
`commands/rm_module.c`; see `notes/iterations/RM-01.md` for the full
veneer this needed (`struct stat`/`lstat` from `STAT-02`, `fts` from
`FTS-CORE-01`, `warnx` from `LIBC-ERR-02`, plus this ID's own
`strrchr`/`memset`/`getchar`/`unlink`/`rmdir`), the two real bugs found
and fixed along the way (an `access()` draft that made `check()`'s
ask-before-removing heuristic fire on every ordinary `rm`, and
`fts_read()`'s errno-clearing contract on a clean end of walk), and
`VFS-05`, the directory-iteration cursor fix `rm -r` on a multi-entry
directory needed and blocked this ID landing until it existed.

## NetBSD `strrchr`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/string/strrchr.c`
- Local path: `upstream/netbsd/common/lib/libc/string/strrchr.c`
- SHA-256: `2a5533ac29b3de543e1e8bdfd34b67a0e0420004e01154edd4b90aa0d3f96d55`
- Embedded RCS identifier: `$NetBSD: strrchr.c,v 1.7 2020/04/07 08:07:58 skrll Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1988, 1993), retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. Same treatment as the
already-imported `strchr.c` sibling: `string.h`'s plain `#define strrchr
cb_libc_strrchr` renames the pinned definition itself (this file has no
`#undef strrchr`, unlike `memset.c` below), so no link-name adapter is
needed. `RM-01`'s own gap: `rm.c`'s `checkdot()` calls `strrchr(s, '\0')`
unconditionally to find its own argument's terminator before trimming
trailing slashes, and `strrchr(s, '/')` to extract the basename.

## NetBSD `memset`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `common/lib/libc/string/memset.c`
- Local path: `upstream/netbsd/common/lib/libc/string/memset.c`
- SHA-256: `0ccb3b88060b85f8a5a01785e2b142f96cbadada6b15970d7adf62e887c580ee`
- Embedded RCS identifier: `$NetBSD: memset.c,v 1.12 2019/03/30 10:18:03 jmcneill Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1990, 1993), retained verbatim in the imported file.

The imported file is byte-for-byte unchanged, but needed more than
`strrchr` did to build and run correctly, both found by a failing test
rather than assumed away:

1. **Link-name adapter required.** Unlike `strchr`/`strrchr`, `memset.c`
   does an unconditional `#undef memset` right after including
   `<string.h>`, which defeats a plain `#define`. Uses the same
   `__asm__("cb_libc_memset")` treatment as `strcpy`/`strcmp`/`memcpy`/
   `memmove`/`memcmp`, gated on `CANNEDBSD_BUILDING_LIBC_MEMSET`.
2. **`u_char`/`u_int`/`u_long` added to `compat/netbsd/include/sys/types.h`**
   — legacy BSD aliases this file uses internally that the existing
   import-only shim didn't have.
3. **`UINT_MAX` added to `compat/netbsd/include/limits.h`** — the word-fill
   fast path replicates a byte pattern across a full word by testing `#if
   UINT_MAX > 0xffff` / `> 0xffffffff`. An undefined `UINT_MAX` evaluates
   to 0 in `#if`, silently skipping the replication steps and leaving the
   upper bytes of every word-sized store zeroed instead of pattern-filled.
   Caught by a failing `memsetprobe` test asserting on filled-buffer
   contents, not by inspection.
4. **Compiled with `-fno-builtin-memset -fno-tree-loop-distribute-patterns`.**
   Without these, GCC's loop-idiom recognition rewrites this file's own
   fill loops into calls back to `memset` — which resolves, via the same
   link-name binding this file itself installs, to this exact function,
   producing unbounded self-recursion and a stack-overflow segfault at
   runtime (not a compile-time symptom). Confirmed via `gdb`'s backtrace
   showing `cb_libc_memset` calling itself over a thousand frames deep.
   `memcpy`/`memmove` avoid this by building at `-Os`, which happens not to
   enable this particular pass on this compiler; `-Os` alone was tried
   here first and did not prevent it, so the two `-fno-*` flags are used
   directly instead of relying on optimization-level side effects.

`RM-01`'s own gap: `rm.c`'s `-P` (secure overwrite) macros call `memset`;
out of scope for the accepted matrix (see `notes/iterations/RM-01.md`), but
the declaration and a correct implementation still need to exist for the
file to parse and for other, in-scope code paths to link against the same
archive member.

## NetBSD `mv`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/mv/mv.c`
- Local path: `upstream/netbsd/bin/mv/mv.c`
- SHA-256: `df5de897a14e2f8210140e468b94319eaf100018490bfebe564152bf337bfd54`
- Embedded RCS identifier: `$NetBSD: mv.c,v 1.46 2020/06/24 16:58:12 riastradh Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

## NetBSD `mv` `pathnames.h`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/mv/pathnames.h`
- Local path: `upstream/netbsd/bin/mv/pathnames.h`
- SHA-256: `82819eb682b4e2e8ec84604d8b9b11532f987427d697989ff21c6aa56fc30d6a`
- Embedded RCS identifier: `$NetBSD: pathnames.h,v 1.8 2004/08/19 22:26:07 christos Exp $`
- License: file-specific three-clause Regents of the University of California
  license, retained verbatim in the imported file.

Both imported files are byte-for-byte unchanged. All adaptation is outside them:
the build renames `main` to `cb_mv_main`, cannedBSD private headers satisfy the
POSIX utility veneer (`sys/extattr.h`, `sys/time.h`, `sys/wait.h`, `signal.h`,
`pwd.h`, `grp.h`, `unistd.h`, `sys/stat.h`, `stdio.h`, `string.h`, `err.h`),
and `commands/mv_module.c` supplies the native-program descriptor. Same-mount
moves use native VFS `rename` as the common path; cross-mount `EXDEV` fallbacks
execute fastcopy with non-fatal `fcpxattr` `ENOSYS` warning absorption.

## NetBSD `cat`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/cat/cat.c`
- Local path: `upstream/netbsd/bin/cat/cat.c`
- SHA-256: `2cc2ced0fcc6c143e1406e64cdd64ea768101fcd19b6dad531b911a611697cbd`
- Embedded RCS identifier: `$NetBSD: cat.c,v 1.60 2023/12/10 15:31:53 rillig Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993), retained verbatim in the imported file.

The imported file is byte-for-byte unchanged. Registered via
`commands/cat_module.c`, replacing a bootstrap-era cannedBSD-owned `cat`
placeholder (plain concat + `-` for stdin, no flags) that occupied the same
name; see `notes/iterations/CAT-01.md` for the full veneer this needed
(`isascii`/`toascii`/`iscntrl` from `LIBC-CTYPE-01`, `strtol` from
`LIBC-STRTOL-01`, `warnx` from `LIBC-ERR-02`, `clearerr`/`setbuf`/`fileno`
from `LIBC-STDIO-02`, `struct stat`/`fstat` from `STAT-02`, `fcntl.h`
declarations and honest `ENOSYS` on lock commands from `FCNTL-01`), the two
real runtime gaps found and fixed under this ID (`fclose(stdout/stderr)`
now honestly succeeds per a revision of `STDIN-01-design.md`; `O_NONBLOCK`
is now accepted and harmlessly discarded by `cb_libc_open` since RAMFS
opens never block), and the one gap still open (`-n`/`-b` need `%d`/width-
`%s` support in the internal formatter -- `FORMAT-01`, excluded from the
accepted matrix until it lands). `cat -l` is documented as outside the
accepted matrix, same precedent as `rm`'s own `-P`/`-W`.

## NetBSD `cp`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/cp/cp.c`
- Local path: `upstream/netbsd/bin/cp/cp.c`
- SHA-256: `fef86b0fbc0161c436a5b6e4c8255c19e86cd9033b71f72f81a3963551d06613`
- Embedded RCS identifier: `$NetBSD: cp.c,v 1.63 2024/06/07 21:01:00 andvar Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1988, 1993, 1994), retained verbatim in the imported file.

## NetBSD `cp` `utils.c`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/cp/utils.c`
- Local path: `upstream/netbsd/bin/cp/utils.c`
- SHA-256: `d20b071192c52f559082183fda12692ea99b6c19beec5680a58ed3475ae99ca2`
- Embedded RCS identifier: `$NetBSD: utils.c,v 1.50 2024/01/15 17:41:06 christos Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1991, 1993, 1994), retained verbatim in the imported file.

## NetBSD `cp` `extern.h`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/cp/extern.h`
- Local path: `upstream/netbsd/bin/cp/extern.h`
- SHA-256: `6299aea50a1f960547bb0f426b3b7bfaed614258aba488287c4efbdc074e5ff6`
- Embedded RCS identifier: `$NetBSD: extern.h,v 1.20 2020/09/07 03:09:55 mrg Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1991, 1993, 1994), retained verbatim in the imported file.

All three imported files are byte-for-byte unchanged. Registered via
`commands/cp_module.c`. `cp` executes entirely in-process using `fts(3)`
directory traversal, VFS `mkdir`, and chunked 64KB read/write fallback loops
when `mmap` returns `MAP_FAILED` (`ENOSYS`). It makes 0 calls to `vfork`,
`fork`, `exec*`, `spawn`, `system`, or `popen`. Compiled with `-DSMALL
-Dmain=cb_cp_main`.

## NetBSD `ls`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/ls.c`
- Local path: `upstream/netbsd/bin/ls/ls.c`
- SHA-256: `385a3c3f495913a04127fe52e082030c57e43e5d1bacf9299b2d7b5b2597787a`
- Embedded RCS identifier: `$NetBSD: ls.c,v 1.79 2024/12/11 12:56:31 simonb Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993, 1994), retained verbatim in the imported file.

## NetBSD `ls` `ls.h`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/ls.h`
- Local path: `upstream/netbsd/bin/ls/ls.h`
- SHA-256: `50610b1281ff61a6171de9c73dab7ee9861e0ab0fbd66118242786abb325dc34`
- Embedded RCS identifier: `$NetBSD: ls.h,v 1.20 2024/12/11 12:56:31 simonb Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993), retained verbatim in the imported file.

## NetBSD `ls` `print.c`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/print.c`
- Local path: `upstream/netbsd/bin/ls/print.c`
- SHA-256: `012336e4f206483f4aaf31a7380889aa677f66b53fc388442766dccacd1ee8a8`
- Embedded RCS identifier: `$NetBSD: print.c,v 1.59 2024/12/11 12:56:31 simonb Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993, 1994), retained verbatim in the imported file.

## NetBSD `ls` `cmp.c`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/cmp.c`
- Local path: `upstream/netbsd/bin/ls/cmp.c`
- SHA-256: `6a43161605d90c8b6a69103356a83cb01dbd8b7032b342335dd04cdc2003e73a`
- Embedded RCS identifier: `$NetBSD: cmp.c,v 1.17 2003/08/07 09:05:14 agc Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993), retained verbatim in the imported file.

## NetBSD `ls` `extern.h`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/extern.h`
- Local path: `upstream/netbsd/bin/ls/extern.h`
- SHA-256: `fef4bb08e410d5b9aee230df8160391383a94d1ce2d0c6d537ad3168d1af7851`
- Embedded RCS identifier: `$NetBSD: extern.h,v 1.17 2011/08/29 14:44:21 joerg Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1991, 1993), retained verbatim in the imported file.

## NetBSD `ls` `util.c`

- Repository: `https://github.com/NetBSD/src`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- Upstream path: `bin/ls/util.c`
- Local path: `upstream/netbsd/bin/ls/util.c`
- SHA-256: `87205a7e649375576afc954f0d58597ebb4db8383c2dbffd1b0379d320f2c88a`
- Embedded RCS identifier: `$NetBSD: util.c,v 1.35 2026/08/15 13:33:36 riastradh Exp $`
- License: file-specific three-clause Regents of the University of California
  license (1989, 1993, 1994), retained verbatim in the imported file.

All six imported files are byte-for-byte unchanged. Registered via the
pre-existing `commands/ls_module.c`, replacing the bootstrap-era
cannedBSD-owned `commands/ls.c` (`LS-01`: single-column, no options, `-l`
a usage error) that occupied the same name; see `notes/iterations/LS-02.md`
for the full veneer this needed (real `device`/`nlink`/`uid`/`gid`/
timestamps from `FS-STAT-01`, `fts_children`/`fts_link`/`fts_parent`/
`FTS_SEEDOT` from `FTS-CHILDREN-01`, dynamic-width/left-justify/unsigned
`%*d`/`%-*s`/`%u` support added to the internal formatter, a real
single-byte-per-character wide-character subsystem and `strvis` for the
default and `-b`/`-B` escaping paths, and a new `wall_clock_millis`
accessor appended to `cb_api_v1` so `time(3)` has a real current time to
read). `-l`'s mode column reads the real per-node mode bits RAMFS has
tracked since node creation (whatever the creating `mkdir`/`open` call
passed) via `strmode(3)`, not a fabricated value; permission enforcement
itself remains deferred for this milestone, same as every other command.
`uid`/`gid` are 0 for the same already-committed reason as `FS-STAT-01`.
`-h`/`-i` and any option depending on `TIOCGWINSZ` (terminal width,
multi-column layout sizing beyond the fixed 80-column fallback) fail
honestly (`ENOSYS`/`ENOTTY`) rather than fabricate a terminal geometry or
humanized size; termcap is out of scope. Compiled with `-DSMALL
-Dmain=cb_ls_main`.
