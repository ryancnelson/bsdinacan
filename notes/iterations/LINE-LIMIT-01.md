# LINE-LIMIT-01 — pinned comm input bound

Candidate base 6abe7f156e21069e15cba7b0df8de6f82328708c contains the reviewed
C-only comparison helpers. Adds LINE_MAX=2048 to the existing import-only
limits shim, matching pinned NetBSD sys/sys/syslimits.h at
b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c. No public limits surface is promised.

Unchanged comm.c SHA256:
b2e98757c0e188d9abfe57aff2b12ed0ac50ee0ec47e57188bda6c3b18ff6eb2.
Strict private-header compilation before the constant fails with seven
diagnostics for LINE_MAX and cascading line2 uses. After adding the constant,
the same command exits zero:

```sh
resource_dir=$(clang -print-resource-dir)
clang -nostdinc -ffreestanding -isystem "$resource_dir/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -ferror-limit=0 -O2 \
  -fsyntax-only /path/to/pinned/comm.c
```

Source inspection: two automatic 2049-byte buffers reserve 4098 bytes before
other stack use. getnextln retains at most 2048 bytes and consumes the remainder
through newline/EOF. Embedded NUL follows inherited C-string behavior. These
are source-derived boundaries, not executed comm acceptance. Compilation does
not prove archive/link closure, execution, guest stack sufficiency or general
line-limit guarantees. No comm source is imported in this candidate.

Tracked publication and diff checks pass. Exact CI and independent review are
pending; inherited comparison runtime changes still need fresh Mac and Solaris
qualification before integration. No guest operation or runtime merge occurred.
