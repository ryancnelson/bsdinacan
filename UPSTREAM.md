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
