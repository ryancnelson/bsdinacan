# REUSE-02 — MoreFiles catalog compile feasibility

Base: `f833163`, freshly fetched origin/main. Worktree:
`bsdinacan-REUSE-02`, branch `work/REUSE-02`. Assigned by the coordinator using
reviewed plan `dd88ee7`; the plan permits stopping at a concrete dependency
blocker. This note owns no backlog/status rollup.

## Hypothesis and source boundary

The unchanged MoreFiles iterator and its immediate helper translation unit can
compile with the existing pinned 68000 Retro68 image, without replacing the SDK
or introducing unverified Toolbox declarations. This hypothesis is **falsified
for the untouched compilation boundary**, before link or guest execution.

Imported nine revision-1.1 files from the official MacPerl CVS snapshot already
verified by REUSE-01. `UPSTREAM.md` and `upstream/morefiles/pins.json` record
source/archive URLs, revisions and hashes. Each file was compared byte-for-byte
to the checkout. Each actual notice was inspected: the iterator carries Jim
Luther/Apple's unrestricted incorporation permission; the other seven carry
Apple's sample-code notice including altered-source attribution terms. All
notices and original MacRoman bytes are retained. No source has been altered.

## Actual compiler evidence

Initial experiment used:

```sh
/Retro68-build/toolchain/bin/m68k-apple-macos-gcc \
  -Os -ffunction-sections -fdata-sections -Wall -Wextra \
  -c PATH/IterateDirectory.c -o /tmp/iterate.o
/Retro68-build/toolchain/bin/m68k-apple-macos-gcc \
  -Os -ffunction-sections -fdata-sections -Wall -Wextra \
  -c PATH/MoreFilesExtras.c -o /tmp/extras.o
```

Image: `ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019`.
The real GCC 16.1.0 diagnostics include:

```text
MoreFilesExtras.h:201:9: error: unknown type name 'XVolumeParam'
MoreFilesExtras.h:231:9: error: unknown type name 'VolumeType'
MoreFilesExtras.h:246:9: error: unknown type name 'Str27'
MoreFilesExtras.h:496:51: error: unknown type name 'UInt64'
MoreFilesExtras.c:28:10: fatal error: Finder.h: No such file or directory
```

Further iterator diagnostics concern AFP structures, access masks and
`afpAccessDenied`. These are source/SDK mismatch evidence, not a failing
cannedBSD runtime assertion. Inspected the SDK include directory and searched
installed Retro68 trees: only Multiversal `Files.h` variants are supplied; no
`Finder.h` alternative was found. A real minimal `Files.h` control calling
`PBGetCatInfoSync` compiles successfully, excluding general compiler/setup
failure as the explanation. Function-section garbage collection cannot repair
missing declarations during compilation. No compatibility macros, empty stubs,
calling-convention changes, alternate CPU or record layouts were invented.

The reproducible `platform/mac68k/probes/morefiles/diagnose.py` now preserves
complete separate logs, commands/statuses, compiler identity, pins and explicit
`viable: false` JSON. Existing-output directories are rejected to preserve old
evidence. The additive CI step is named `diagnose-morefiles-not-viable`; its
success means the documented blocker reproduced, **not compilation passed**.
Unexpected source drift, setup failures or changed outcomes fail the step.
The ordinary CannedBSD target and artifact are unaffected.

## Outcome and remaining acceptance

**Not yet viable within this slice.** Linking was not attempted because neither
MoreFiles translation unit compiled. No probe CODE size, HFS fixture, guest
callback/metadata/error result, or fixture-preservation claim exists. No guest
was launched or controlled. The ordinary CannedBSD guest smoke would not resolve
this source dependency failure.

A separate compatibility task should compare a licensed/pinned Universal
Interfaces SDK against a reviewable minimal helper extraction. The reachable
catalog/volume helper bodies are small, but extracting them and substituting
header declarations changes the import contract and needs explicit review and
ABI validation. After a real compile/link succeeds, retain every fixture,
callback, metadata, native-error and read-only hash requirement from the plan.
Do not infer `/Host` viability, portable VFS semantics or access-denial behavior
from this diagnostic. No core, libc, normal Mac app or guest runner changed.

## Validation

Local pinned-image diagnostic reproduced both compile failures with a passing
SDK control. Source checks cover all nine original byte hashes. A temporary
copy with an altered iterator byte was rejected before compiler invocation;
reusing its evidence directory was also rejected. Python syntax, shell syntax
and `git diff --check` pass. Exact-commit
Woodpecker validation and artifact identifiers are recorded at handoff after
push. No runtime guest acceptance is applicable to diagnostic-only imported
inputs that are never linked into an application.

## Exact CI evidence and integration refresh

Feature `057470d006cd8cfb497b73909af94bbe02f31608` ran Woodpecker #105.
The `mac68k` and `mac-automation` workflows passed; the separate diagnostic
archive was fetched and checked against its published SHA256SUMS:
`2a55860b47ecd702718dbbef942df45e2c78c023f7af9eb5d166d6386d8de557`.
Its embedded commit/pipeline match; the SDK control compiled and both MoreFiles
units exited 1 with the documented blocker. This is failed feasibility evidence,
not a guest artifact. Local full Alpine `make LDLIBS=-lucontext SANITIZE_CC=clang
ci` passed.

The #105 Linux workflow failed at the printenv diagnostic test because concurrent
local-backend jobs shared fixed `/tmp` files. Both #104 and #105 ran that gate
during overlapping timestamps and failed. The coordinator separately implemented
CI-01 (`0ebfd2f`), replacing shared captures with unique per-run storage and
verifying eight concurrent real-binary tests. With explicit authorization, this
branch merged freshly fetched `origin/main` at `0ebfd2f` without conflicts
(merge `ed3c9e9`), including the already integrated ERR-01 work. No REUSE-02 source,
probe scope or expected outcome changed. Exact refreshed workflow statuses and
the newly published diagnostic checksum are supplied at handoff; #105's failed
Linux gate is not represented as an all-green result.
