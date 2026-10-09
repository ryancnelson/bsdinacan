# UNIQ-ADMISSION-01 — strict compile after measured prerequisites

Documentation-only coordinator measurement. Accepted-main comparison is
`a520a06d4866529bdd63f65bd1b98e5b603c17f7`; runtime candidate comparison is
`39e45282c64904919cbcb50236f034139acc2654`. No source import, header change,
command registration, link or execution is performed by this task.

Downloaded unchanged usr.bin/uniq/uniq.c at NetBSD pin
b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c. SHA256 matches NEXT-UTIL-03:
78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195.

From each comparison tree, using Apple Clang and a temporary source/object:

```sh
resource_dir=$(clang -print-resource-dir)
clang -nostdinc -ffreestanding -isystem "$resource_dir/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -ferror-limit=0 -O2 \
  -c /path/to/pinned/uniq.c -o /path/to/scratch/uniq.o
nm -u /path/to/scratch/uniq.o
```

Accepted main exits 1: fgetln undeclared at 125, pointer-conversion cascades at
125/142, and asprintf undeclared at 249 (four errors). The candidate exits 0
with no diagnostics. No substitute headers, injected prototypes, source edits,
host headers or relaxed warning flags were used. Thus the previously measured
strict command compilation gap is closed on the candidate, not on main.

The candidate object has 22 undefined symbols, all cb_libc-prefixed (after
removing the platform's leading underscore): asprintf, err, errx, exit, fgetln,
fopen, fprintf, free, getopt, getopt_state_location, getprogname, isdigit,
isspace, malloc, memcpy, realloc, setprogname, stderr_stream, stdin_stream,
stdout_stream, strcmp and strtol. This is object-level boundary evidence only;
no complete archive link closure or runtime behavior is claimed.

Next remains UNIQ-01 after prerequisite qualification: unchanged source import,
typed isolation of six static integer fields, repeated/conflicting invocations,
pipelines, FILE/stdin/output forms and actual numeric/legacy options. Preserve
and characterize upstream's unchecked read errors and C-string binary limits.
Fresh Mac and Solaris qualification still block runtime integration. The
candidate's exact CI is in progress at this measurement; static review is clean.
This documentation task does not require guest acceptance.
