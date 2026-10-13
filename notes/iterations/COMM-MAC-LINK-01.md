# COMM-MAC-LINK-01 — scratch Retro68 link and compiler frames

Study only, from exact candidate base
`1559aed82d567b6c9d1d58bd392973b76be998e8`, branch
`work/COMM-MAC-LINK-01`. Only this note is committed. The coordinator explicitly
assigned this target follow-up to COMM-ADMISSION-01. No tracked source import,
command registration, runtime/header/build edit, command execution or guest
operation. The coordinator owns rollups and target qualification.

Hypothesis: unchanged pinned comm compiles through the real private mappings and
links into the actual Mac application with its descriptor retained, rather than
being discarded by section garbage collection. This is a link control, not a
behavioral red/green test or command acceptance.

## Source, toolchain and baseline

Pinned NetBSD revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, comm.c
revision 1.20. Source SHA256 before transfer and after build/control:
`b2e98757c0e188d9abfe57aff2b12ed0ac50ee0ec47e57188bda6c3b18ff6eb2`.
The BSD license and bytes remain unchanged in scratch storage. A git archive of
the exact base supplies the real runtime, private headers and Mac build.

Network-disabled Retro68 image:
`ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019`.
Observed image ID:
`sha256:72df6b29ee42ea9eb158464c72a48b1c3de5b58f80726b55eaed75383c29aa02`.
Compiler: `m68k-apple-macos-gcc (GCC) 16.1.0`.
Compiler predefined macros report four-byte int, long and pointers, and
big-endian byte order.
The unchanged baseline configured and built the complete application with exit 0:

```sh
cmake -S platform/mac68k -B build-mac68k \
  -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-mac68k -j2
```

## Strict object and actual application link

In the disposable archive copy only, add an OBJECT target for scratch/comm.c
with `main=cb_comm_mac_main` and `_XOPEN_SOURCE=700`. Existing include/src paths
plus compat/netbsd/include and libc/include precede target system headers.
The emitted flags retain the actual Mac configuration `-Os -ffunction-sections
-fdata-sections -Wall -Wextra`, and add `-nostdinc -ffreestanding -std=c99
-Werror -Wpedantic -fstack-usage`. Explicit system directories are the compiler's
`-print-file-name=include` result and the pinned target SDK directory
`/Retro68-build/toolchain/m68k-apple-macos/include`. No host system-header
directory, injected declaration or fallback function is supplied.

The first scratch attempt hid target stdint.h with -nostdinc. A subsequent
attempt was also a setup failure because CMake deduplicated separate -isystem
arguments, leaving the SDK directory without its option. Grouping the SDK option
as `"SHELL:-isystem /Retro68-build/toolchain/m68k-apple-macos/include"` corrects
the actual emitted command. Neither failure is a missing comm interface or
behavioral red. The corrected strict comm/wrapper compilation exits 0.

The separate ordinary descriptor wrapper is exactly:

```c
#include "cannedbsd/libc.h"
int cb_comm_mac_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(cb_comm_mac_program, "comm-scratch", cb_comm_mac_main);
```

Compile it with the same strict freestanding/system-header flags, using only the
ordinary include/src paths. Add both scratch OBJECT targets to CannedBSD's real
target_sources, and add `-fstack-usage` to that target to report libc/core/host
frames. Keep existing single-segment/resource link options and add
`-Wl,-u,cb_comm_mac_program -Wl,-Map,comm-scratch.map`. Reconfigure and rebuild
the complete application: exit 0. No registry entry is added.

The command object's nm -u output contains exactly 17 undefined symbols:
cb_libc_err, cb_libc_exit, cb_libc_fclose, cb_libc_ferror, cb_libc_fopen,
cb_libc_fprintf, cb_libc_getc, cb_libc_getopt, cb_libc_getopt_state_location,
cb_libc_printf, cb_libc_setlocale, cb_libc_stderr_stream, cb_libc_stdin_stream,
cb_libc_stdout_stream, cb_libc_strcasecmp, cb_libc_strcmp, cb_libc_strcoll.
All are private; there is no unprefixed command dependency.

The link map retains the implementation at `0x0001e58c`, the start-wrapper text
section, and the descriptor at `0x0002e8d4`. A negative control removes only
the comm source object from CMakeFiles/CannedBSD.dir/link.txt and redirects its
output/map to separate control files. The otherwise identical link exits 1:
`undefined reference to cb_comm_mac_main` from cb_comm_mac_program_start.
This demonstrates that descriptor retention cannot silently discard the command
and mask a missing implementation. The successful application remains intact.
nm on CannedBSD.code.bin.gdb independently reports those retained symbols;
nm -u has no strong undefined symbols, only weak __deregister_frame_info and
__register_frame_info toolchain references. CannedBSD.code.bin is a raw linked
image, so the first nm attempt on it was an inspection-format error, not a
link failure.

The checked-in check_code_resources.py passes: one executable CODE segment.
Scratch successful CannedBSD.code.bin SHA256:
`df179f58a4c93292620f8ce256a7023e3ba659f7b0826a4ee1712a4caf408a17`.
Scratch .rsrc/CannedBSD.APPL SHA256:
`2d7089649d49d67f5c1deef5aa036579484eace1db5a28ba59556a76fb28fdc6`.
These are link-evidence outputs, not published or staged acceptance artifacts.

## Compiler frame evidence and remaining gates

The comm .su report gives these bytes, all `dynamic,bounded`:

| Function | Bytes |
| --- | ---: |
| cb_comm_mac_main | 4156 |
| getnextln | 16 |
| show | 20 |
| file | 12 |
| usage | 12 |

The wrapper start is 0 bytes, `static`. Actual runtime reports include getc 28,
read_input 36, printf/fprintf 12 each, format_output 96, write_all 24, err 48,
api_read/api_write 24 each, pipe_read/pipe_write 56 each and api_exit 20, all
`dynamic,bounded`. strcoll is 0 bytes static; strcasecmp is 4 bytes static.
Mac host context_switch reports 12 bytes and yield_host 4, dynamic,bounded;
root-dispatch allocate_on_root/release_on_root are 0 bytes static.
These are individual compiler frames, not a summed call-chain peak, minimum
safe stack, guard/canary measurement or proof that the generic 64 KiB task stack
is sufficient. Indirect API dispatch, scheduler/assembly boundaries and host
root-stack transitions still need actual execution evidence. No stack budget
change is justified by this study alone; HEAD-01's frame-report caveat applies.

The inherited two 2049-byte buffers, truncation/embedded-NUL/input-error behavior
and forthcoming ownership/pipe fixtures remain as described in the reviewed
admission/source design. Compile and link do not exercise any of them. Solaris
9 SPARC object/link closure remains unmeasured. Runtime prerequisites still need
fresh exact Mac guest and Solaris qualification before integration.

Validation for the tracked note is diff/publication hygiene, followed by exact
hosted documentation CI handled by the coordinator. No local full make ci,
command execution, guest boot or Solaris run is claimed. Documentation-only
guest acceptance is not required. Scratch logs, map, source, objects and .su
reports are preserved for independent review; no shared symbols were touched.
