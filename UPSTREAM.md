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
