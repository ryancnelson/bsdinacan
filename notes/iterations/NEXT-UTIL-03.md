# NEXT-UTIL-03 — next utility after the ls/tee candidate

## Decision and scope

Recommend **uniq**, after ls/tee qualification and the prerequisites below.
Neither candidate compiles yet. This is a dependency investigation, not an
import or an execution claim for either utility.

- Assigned candidate: `f4e3f2544772ad1cae7e17396e0449ee27e10f9d`.
- Accepted-main comparison: `3ca4993f7f0a20e4983e69a1452dd43d48237fbf`.
- Branch: `work/NEXT-UTIL-03`, isolated sibling worktree.
- Changed file: this note only. No runtime, header, source-pin or guest changes.

The hypothesis was partly confirmed: ls added real util/wide-character/header
surface and the reusable task-state wrapper. However, the direct uniq compile
gaps are identical on these two revisions. Both already contain strtol,
memset and signed decimal formatting; those are improvements over the older
NEXT-UTIL-02 inventory, not new differences caused by this candidate.

## Source identity and reproduction

Downloaded the same three files as NEXT-UTIL-02 from
`https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/`.
All hashes matched that existing inventory:

| Path | SHA-256 |
| --- | --- |
| `usr.bin/uniq/uniq.c` | `78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195` |
| `usr.bin/cut/cut.c` | `7710a0db344af6cf879cbf5bde89e85a380dee37dda6a9ab08cb6318c36a011f` |
| `usr.bin/cut/x_cut.c` | `3c7415ac42534c7f75a3a3a232d02a3c6e50b7808b6d94ae43008e0cc12b1271` |

Place them unchanged in a temporary source directory; cut includes x_cut.c
from that directory. Extract each repository revision separately with
`git archive REVISION`. From each extracted repository root, run:

```sh
resource_dir=$(clang -print-resource-dir)
clang -nostdinc -ffreestanding -isystem "$resource_dir/include" \
  -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -ferror-limit=0 -O2 \
  -c /path/to/pinned/uniq.c -o /path/to/scratch/uniq.o
# Repeat with cut.c and a separate cut.o.
make -j4 build/libcannedbsd.a
nm --defined-only build/libcannedbsd.a
```

No empty substitute headers, injected prototypes, source transformations,
relaxed diagnostics or host-header/library fallbacks were used. `-nostdinc`
and freestanding Clang permit only the explicit private headers and compiler
fundamental headers. The actual library archives use their ordinary Makefile
build rules; they were inspected, not linked to the failed command objects.

Measurements agreed on Apple Clang 17 (arm64 macOS) and Alpine Clang 20.1.8
(x86_64 musl). The two actual archives built successfully with GCC 14.2.0 and
were inspected with GNU nm 2.44 in the isolated Linux agent image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
with network disabled. Only the archives were built; no full local CI or
utility execution was performed.

## Compile and symbol evidence

| Source | Accepted main | Post-ls/tee candidate |
| --- | --- | --- |
| uniq.c | Exit 1: undeclared fgetln at 125, pointer-conversion cascades at 125/142, undeclared asprintf at 249; four errors | Identical four errors, exit 1 |
| cut.c including x_cut.c | Exit 1 at cut.c:59: fatal `util.h` not found | Exit 1, twenty diagnostics: nine distinct missing names below, conversion cascades and a signedness diagnostic |

Candidate cut's direct missing names:

- `_POSIX2_LINE_MAX` at 164/200; `roundup` at 200.
- `ecalloc` at 165; `erealloc` at 201; `strtok` at 174.
- `fgetln` at 236/295; `mbrlen` at 238.
- `getwc` and `putwchar` through x_cut.c's character-mode expansion.

The candidate also diagnoses x_cut.c:72's comparison of unsigned `wint_t`
with signed `EOF` under `-Werror,-Wsign-compare`. Do not claim adding the
missing prototypes alone produces a strict green build. A future cut owner
must remeasure and resolve this source-preserving compatibility boundary.

Both built archives contain **neither raw nor cb_libc-prefixed definitions**
for fgetln, asprintf, ecalloc, erealloc, strtok, mbrlen, getwc or putwchar.
Both define cb_libc_strtol, cb_libc_memset, cb_libc_fopen, cb_libc_fprintf and
cb_libc_mbrtowc. Only the candidate defines cb_libc_wcrtomb and
cb_libc_snprintf. Since strict command compilation fails, there is no valid
uniq/cut object or complete command undefined-symbol/link closure to report.
The archive inventory establishes missing implementations, not just headers.

Source inspection separates what the main cut header failure masks:
mbstate_t, wint_t and __unused already exist on main, while WEOF and util.h
are candidate additions. The new util.h supplies getbsize/flags_to_string,
not cut's ecalloc/erealloc. Candidate snprintf does not supply asprintf and
supports no `%c` conversion; uniq's actual allocating format is `"-%c%s"`.

## Semantic gaps hidden by declarations

Uniq's two-file form calls `fopen(output, "w")` at uniq.c:119. On both
revisions cb_libc_fopen accepts only `r`/`rb`, returning NULL/EINVAL for `w`.
Furthermore cb_libc_fprintf accepts only the stdout/stderr identities, rejecting
other FILE pointers. Merely enabling file creation therefore cannot implement
uniq's output-file path. cb_libc_fwrite has the same output-identity restriction.

Conversely `fprintf(ofp, "%4d %s", repeats + 1, str)` at uniq.c:192 needs no
new decimal/width support: the implemented formatter and existing format
probe cover these conversions on both revisions. This investigation inspected
that implementation and its tests; it did not rerun a uniq count fixture.

Fgetln needs a real borrowed line buffer, including a final unterminated line,
length including any newline, EOF/error state and errno. Its lifetime is tied
to subsequent I/O on that stream and close; it cannot be one global buffer.
See the pinned [fgetln contract](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/stdio/fgetln.3).

Uniq ignores asprintf's return and checks the pointer instead. Failure must
leave that pointer NULL, as the pinned
[vasprintf implementation](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/stdio/vasprintf.c)
does. Returning -1 with an indeterminate pointer would not meet this consumer.

## Concrete prerequisite queue proposal

These are proposed IDs, not claims or permission to implement. Recheck branch
and shared-symbol ownership before assignment. No matching fgetln/asprintf/
uniq/cut implementation branch was found in the local/remote branch inventory.

1. **STDIO-WRITE-01 — writable file streams for uniq.** Own fopen's bounded
   `w` path, FILE write dispatch through fprintf, close/error semantics and
   corresponding stream metadata. Reuse raw open/write/truncate primitives.
   Prove creation, truncation, partial/zero-progress failure, foreign/stale
   stream rejection and cleanup on exit, exec and teardown. Preserve read
   streams and stdout/stderr; reject other unsupported modes honestly. Make
   the treatment of other output APIs such as fwrite explicit rather than
   accidentally accepting an unusable FILE. This owns the shared FILE layout.
2. **FGETLN-01 — task-owned line reads.** Follow STDIO-WRITE-01's agreed FILE
   layout (do not race edits to the same stream representation). Own fgetln,
   per-stream line storage and close integration, including stdin's per-task
   state. Test empty input, empty lines, long growth, missing final newline,
   embedded NUL lengths, partial/error reads, allocation failure and two
   interleaved tasks/streams. Clear references before reclaiming allocations;
   preserve old-size/absent-state capability failures where applicable.
3. **ASPRINTF-01 — measured allocating format.** Can be claimed separately
   from stream work: own asprintf and the private declaration, with the
   measured literal/`%c`/`%s` path, checked lengths and task-owned allocation.
   Test `-3`/`+3` legacy option conversion through `"-%c%s"`, allocation failure
   with NULL output, and honest rejection beyond its stated bounded surface.
   Do not assume candidate snprintf already covers this consumer.
4. **UNIQ-01 — unchanged import and task state.** Only after the above and
   ls/tee qualification. Reuse the existing executor slot mechanism, preserving
   all six file-scope ints: cflag, dflag, uflag, numchars, numfields, repeats.
   They initialize to zero but are not reset by main. Save/restore across
   yields, not only entry reset. Test repeated conflicting flags/counts and
   overlapping pipelines, plus creation failure, termination, exec and a later
   kernel. Globals become typed slots via the established build globalization
   approach; automatic line pointers remain task-stack state.

Uniq frees its two automatic line buffers on normal completion, but its
legacy-option allocation is left to task exit. It does not fclose input or
output files. Existing core descriptor/heap cleanup is the starting mechanism,
not permission to leave new line-cache or stream references dangling; test
cleanup before reap or later teardown can hide it. No persistent heap is
needed. The candidate's generic wrapper is already present; main lacks it.

## Cut deferral and inherited behavior limits

Cut would share fgetln but additionally needs bounded ecalloc/erealloc failure
semantics, task-safe strtok continuation state, the two allocation macros,
C-locale mbrlen/getwc/putwchar and the strict signedness issue above. Source
state is larger: bflag/cflag/dflag/fflag/nflag/sflag, dchar, autostart/autostop/
maxval, positions and numpositions. dchar resets in main but must still be
isolated during yields; positions points at task-owned allocation and is never
freed by the command. These are concrete reasons to prefer uniq, not a claim
that any currently missing cut path is supported.

Pinned uniq treats fgetln NULL as end of input without checking ferror, ignores
fprintf failure and normally returns zero. A correct libc must report errors
honestly, but cannot make those ignored results into command failure without
a separate policy decision. Uniq also narrows strtol results to int without
an errno/range guard; repeats is int. Characterize these inherited limits with
long/unterminated lines, NUL input, numeric boundaries and injected I/O faults
when importing. No observed runtime bug or repaired exit behavior is claimed
by this compile/source investigation.

No full cross-platform acceptance claim: Linux/macOS compile evidence and
Linux archive definitions do not qualify Retro68 or Solaris execution. Main
and guests were untouched. Only this note is committed; raw source and logs
remain outside the repository. Final review/CI status is reported separately.
