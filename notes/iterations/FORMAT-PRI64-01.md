# FORMAT-PRI64-01 — match fixed-width printf macros to actual types

- Assigned base: `f66aab307930fa2b048c1b511335fbb88c065657`.
- Branch: `work/FORMAT-PRI64-01`, isolated sibling worktree.
- Hypothesis: attributed compilation of the unchanged pinned ls and
  humanize_number sources exposes the LP64 type mismatch, and choosing each
  format modifier from the actual target typedef removes that mismatch without
  changing imported sources or the formatter's supported conversions.

## Design

`tools/printf64-flags.sh` compiles two compatible external declarations of one
object, first with the target's `int64_t`/`uint64_t`, then with a candidate
`long`/`unsigned long` or `long long`/`unsigned long long`. Conflicting types
are compilation errors, not width comparisons or warning-dependent pointer
conversions. Signed and unsigned types are probed independently. Nothing is
linked or executed, so this works with cross compilers and uses only ordinary
C99 declarations, including for the Solaris GCC 3.4 toolchain.

Make passes its actual compiler, CPPFLAGS, and CFLAGS, including the Solaris
stdint adapter when configured. CMake passes the Retro68 cross compiler and
C flags. The selected definitions reach all ordinary command and probe
compilations. The private inttypes header rejects missing/unknown selections;
it has no guessed fallback and imports no additional host inttypes API.
The existing formatter already reads `long` for `l` and `long long` for `ll`;
no runtime implementation or shared ABI change is needed.

The build probe is preferable to compiler-name tests, width guesses, modern
compiler-specific predefined format macros, or importing a host inttypes
header with callable declarations. Linux LP64 uses `long`, whereas the local
Mac LP64 compiler uses `long long`: equal widths do not establish type identity.
The tradeoff is a few small compile-only probes per Make invocation or CMake
configuration. Unsupported typedefs fail explicitly.

`check-printf64` injects private format-attributed declarations into the actual
pinned consumers with `-Wformat -Werror`. Only this audit omits ISO-pedantic
warnings because pinned ls intentionally uses POSIX grouping; ordinary builds
retain their existing warnings. The audit is required by `make test` and the
Solaris build script. Retro68 applies the same attributes and fatal format
warnings directly to the two real object targets.

## Evidence

Pinned Linux container `tribblix-woodpecker-agent:3.18.0`, image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
network disabled, disposable full clone:

- Assigned-base `make LDLIBS=-lucontext clean test`: passed.
- Baseline compile control, before copying the corrected header/build files:
  `cc -std=c99 -Wformat -Werror -D_XOPEN_SOURCE=700 -DSMALL -Iinclude -Isrc
  -Icompat/netbsd/include -Ilibc/include -include tests/printf64_attributes.h
  -fsyntax-only upstream/netbsd/bin/ls/print.c` failed at lines 146 and 377:
  `%llu` expects `unsigned long long`, actual `uint64_t` is `unsigned long`.
  The same audit of `upstream/netbsd/lib/libc/gen/humanize_number.c` failed at
  lines 172 and 194: `%lld` expects `long long`, actual `int64_t` is `long`.
  These are actual call-site type diagnostics, not width errors.
- Corrected compile audits pass on Linux and local Mac Clang. The Linux probe
  selects both `l` modifiers; the Mac host selects both `ll` modifiers.
- New ordinary-source runtime case checks uint64 zero/MAX and int64 MIN/MAX
  through printf and snprintf, plus independent long/long-long formatting
  through fprintf and snprintf. Existing format tests remain intact.
- Full Retro68 CMake configure/build passed with the pinned image
  `ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019`,
  including the actual attributed pinned-source compilations.
- Full Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: pending final run.
- Build parity and `git diff --check`: passed.

Exact pushed-commit Woodpecker results and independent review are reported in
handoff. No guest was operated. Mac artifact execution and native Solaris
GCC 3.4/runtime qualification remain coordinator gates; compile-only design
compatibility is not a claim of native Solaris acceptance. Shared API symbols
and imported files are unchanged; the only public-facing changes are the
private PRIu64/PRId64 macro definitions and their build configuration.
