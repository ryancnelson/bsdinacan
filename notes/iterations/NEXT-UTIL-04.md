# NEXT-UTIL-04 — measured comm prerequisites after uniq preparation

Read-only coordinator dependency study. Accepted-main comparison is `5865993`;
runtime comparison is `39e4528`. No comm import, link or execution occurs here.
The independent UNIQ-01 candidate remains the current implementation task.

Pinned NetBSD usr.bin/comm/comm.c at b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c
has SHA256 b2e98757c0e188d9abfe57aff2b12ed0ac50ee0ec47e57188bda6c3b18ff6eb2.
Downloaded unchanged into temporary scratch storage. From both trees:

```sh
resource_dir=$(clang -print-resource-dir)
clang -nostdinc -ffreestanding -isystem "$resource_dir/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -ferror-limit=0 -O2 \
  -fsyntax-only /path/to/pinned/comm.c
```

Both exit 1 with nine diagnostics: three distinct missing names, LINE_MAX,
strcoll and strcasecmp, plus repeated uses and undeclared-line2 cascades.
No substitute headers, injected prototypes or relaxed flags were used. A source
search of the private headers/implementation also finds none of these names;
no archive link closure or full gate was run for this measurement.

Recommend comm after uniq qualification, with three small prerequisites rather
than speculative general locale work:

- COLLATE-C-01: real strcoll for the existing C-only locale, preserving unsigned
  byte lexical ordering and ordinary source mapping. No other locale support.
- STRCASECMP-C-01: unsigned-byte ASCII case-insensitive comparison under the
  same locale, exact prefix/high-bit/termination behavior and source mapping.
- LINE-LIMIT-01: document and expose the measured input-line limit. The pinned
  NetBSD sys/sys/syslimits.h defines LINE_MAX as 2048; comm allocates two
  LINE_MAX+1 stack buffers. Do not invent an unlimited-line promise.

These are proposed queued tasks, not completed functions. Implementation must
use the actual private interfaces and meaningful ordinary-source tests. No
success stubs or host locale calls. Each shared symbol needs a single owner.

The later comm task needs two independently owned FILE inputs, all -123 column
combinations, -f case-insensitive sorting, stdin '-' forms, empty and final
unterminated input, exact tab columns, and documented overlength truncation.
Its source uses getc rather than fgetln. It does not check input ferror, but it
checks output failure and closes stdout. Test those inherited distinctions and
child descriptor isolation honestly. The tabs array is read-only by use; flags,
FILE pointers and buffers are local, so no mutable flag slots are demonstrated.

Documentation validation: tracked publication and diff checks only. Candidate
and accepted-main compilation gaps are identical; uniq's new line interface
alone does not close comm's three independent gaps. No guest acceptance is
required for this study; future runtime changes retain every qualification gate.
