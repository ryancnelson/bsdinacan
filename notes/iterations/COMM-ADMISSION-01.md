# COMM-ADMISSION-01 — unchanged comm object and native link closure

Scratch-only study from exact candidate base
`1559aed82d567b6c9d1d58bd392973b76be998e8`, branch
`work/COMM-ADMISSION-01`. Only this note is committed. No source import,
command registry entry, compatibility API, execution or emulator operation.
The coordinator owns the queue and qualification rollup.

Hypothesis: the candidate C-only comparison helpers and LINE_MAX constant close
all actual ordinary-source compile and native archive-link dependencies of
pinned NetBSD `usr.bin/comm/comm.c`. NEXT-UTIL-04 previously measured missing
strcoll, strcasecmp and LINE_MAX; LINE-LIMIT-01 documented strict syntax-only
closure. Those historical missing-interface diagnostics are not a new runtime
red test here.

The cached source retains its BSD license and NetBSD revision 1.20 header.
Pinned repository revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
SHA256 before local transfer and before/after compilation/linking:
`b2e98757c0e188d9abfe57aff2b12ed0ac50ee0ec47e57188bda6c3b18ff6eb2`.
All measurements use these unchanged bytes in scratch storage outside the
tracked source tree. No empty headers, injected declarations or fallback symbols.

## Object and link evidence

A fresh full clone checked out the exact base in the network-disabled pinned
Linux image `tribblix-woodpecker-agent:3.18.0`, image ID
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`.
Compiler reports Alpine clang 20.1.8. From the clone root:

```sh
resource_dir=$(clang -print-resource-dir)
clang -nostdinc -ffreestanding -isystem "$resource_dir/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -ferror-limit=0 -O2 \
  -Dmain=cb_comm_admission_main -c scratch/comm.c -o scratch/comm.o
nm -u scratch/comm.o
```

Exit 0, no diagnostics. Exactly 17 undefined symbols, all private:
cb_libc_err, cb_libc_exit, cb_libc_fclose, cb_libc_ferror, cb_libc_fopen,
cb_libc_fprintf, cb_libc_getc, cb_libc_getopt, cb_libc_getopt_state_location,
cb_libc_printf, cb_libc_setlocale, cb_libc_stderr_stream, cb_libc_stdin_stream,
cb_libc_stdout_stream, cb_libc_strcasecmp, cb_libc_strcmp, cb_libc_strcoll.
No unprefixed host dependency occurs in the command object.

The minimal scratch wrapper contains only:

```c
#include "cannedbsd/libc.h"
int cb_comm_admission_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_comm_admission_program, "comm-scratch", cb_comm_admission_main);
int main(void) { return 0; }
```

It is a private descriptor linkage control, never registered or executed.
Compile it with the same freestanding/builtin-header and strict warning flags,
using only `-Iinclude -Isrc`, without renaming its main. Then:

```sh
make LDLIBS=-lucontext -j4 build/libcannedbsd.a
clang scratch/wrapper.o scratch/comm.o build/libcannedbsd.a -lucontext \
  -Wl,-Map,scratch/link.map -o scratch/comm-link
nm -u scratch/comm-link
```

Archive build and executable link both exit 0. No remaining undefined cb_libc
symbols. Dynamic undefined names are __libc_start_main, __stack_chk_fail and
memset plus weak _ITM_deregisterTMCloneTable, _ITM_registerTMCloneTable,
__cxa_finalize, __deregister_frame_info and __register_frame_info. Archive
inspection identifies cb_libc.o as the requester of __stack_chk_fail and memset;
the other names are host executable startup/toolchain machinery. This does not
claim a freestanding executable without host runtime dependencies. The private
command boundary itself is closed, and no extra library shim was supplied.
The binary was not run.

## Remaining closure gaps

This proves native object/archive linking only. Mac68k and Solaris object/link
closure, target stack sufficiency and command behavior remain unmeasured.
No full make ci or hosted workflow is claimed for this scratch measurement;
only staged diff and publication checks validate this documentation change.
Guest acceptance is not required for the note and was not attempted. Candidate
comparison prerequisites still need their own qualification before integration.

Source inspection, not runtime acceptance: two automatic LINE_MAX+1 buffers
reserve 4098 bytes before the call chain. The sole static tabs array contains
three immutable-by-use string pointers; nm reports local data `tabs` (24 bytes
on this LP64 build), but the source never writes it or its targets. No mutable
command flag slot or extra state wrapper is demonstrated. Flags, comparison
function pointer, FILE pointers and line buffers are local. Existing task-owned
getopt, FILE and libc execution lifecycle must be exercised by a later command
fixture rather than inferred from this link.

A later import needs exact three-column tabs and all eight -123 combinations,
-f ASCII case comparison under the C/POSIX-only locale, sorted input handling,
empty inputs, both stdin '-' forms including aliasing one shared stream,
independent input ownership, tail draining, missing files and bad options.
Characterize unterminated lines, embedded NUL, and 2048/2049-plus byte lines:
getnextln consumes an overlength tail through newline/EOF while retaining at
most 2048 bytes. Input ferror is unchecked and can look like EOF, whereas stdout
ferror or failing fclose leads to err(1). Prove exact diagnostics/status and
cleanup before reap, sequential/reentry behavior, and real pipe suspension with
conflicting invocations. Do not normalize these inherited behaviors or promise
broader locale support from compile/link evidence. No speculative new interface
is justified by this study.
