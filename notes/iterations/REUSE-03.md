# REUSE-03 — bounded MoreFiles catalog derivative

Base: freshly fetched origin/main `819a964`; worktree `bsdinacan-REUSE-03`,
branch `work/REUSE-03`. Assigned by coordinator; no shared status edits.

Hypothesis: the useful single-directory algorithm can compile against the
existing SDK and correctly enumerate a protected HFS fixture without importing
unrelated Universal Interfaces records. REUSE-02's real failed compile remains
the untouched-source baseline. This follow-up explicitly owns and identifies a
small altered derivative; original upstream bytes remain untouched.

Scope: resolved volume/directory identity, copied callback metadata, bounded
catalog indices, native-error propagation, separate probe application/fixture.
All Toolbox calls stay on the app's original stack. No core VFS, libc, polling,
normal app, or emulator runner modifications. The coordinator alone owns the
serialized guest slot. No guest success is claimed before exact CI execution.

## Implementation and local evidence

The SDK-only owned derivative compiled and linked unchanged after its first
implementation; no manufactured failing test is claimed. The useful earlier
red is REUSE-02's actual missing SDK types/header, which this explicit extraction
avoids. Native guest behavior remains the falsifiable test for the derivative.
The app uses single-directory callbacks, copies file/directory records with
exact SDK fields, bounds the signed enumeration index with an extra EOF query,
and propagates access errors. Thirteen guest checks include exact fixture IDs,
type/fork metadata, two independent scans, early stop, missing/file-as-directory
errors, null callback and injected native-error/capacity boundaries.

The real HFS fixture initially exposed the libhfs path convention: a nested
`Subdir:Sentinel` was treated as a volume path. The generator now supplies an
explicit `CatalogFixture:` prefix. This was fixture setup evidence, not a Mac
runtime test. Repeated pinned-image builds then produced identical 2 MiB HFS
bytes, SHA256 `072129060a85379fb701ec07988dcfb900998c6541f4a5315dba6f0d54f6835f`.
The generator verifies required names/fork lengths, unique IDs and this pinned
image hash before compiling generated expectations. Its host-only clock override
freezes libhfs timestamps, without changing compiler or guest time.

The local pinned Retro68 build passed `-Wall -Wextra -Werror` and the existing
CODE 0/1 resource checker. A packaging smoke exposed that `.code.bin` is MacBinary,
not ELF; the code-size report now reads the toolchain's `.code.bin.gdb` ELF.
The dedicated archive uses `CannedBSD:CannedBSD` only to reuse the proven Finder
launcher, with a distinct MoreFilesProbe archive and visible window title. It
writes a separate `morefiles-result.txt`, remains visible for screenshots, and
closes normally on Return after inspection. No general UI runner was changed.

Exact final CI statuses/archive checksum and serialized guest outcome will be
recorded at handoff. Until guest execution succeeds, this is compile/link
feasibility, not a viable host-volume adapter. See the probe README for mandatory
read-only fixture prefix, fresh precreated evidence, clean shutdown, and complete
post-shutdown image hash verification. No core VFS/poll or normal app changed.

Validation before push: full local Alpine `make LDLIBS=-lucontext
SANITIZE_CC=clang ci` passed; package smoke, shell syntax and `git diff --check`
passed. Independent review requested complete stderr capture in archived build
logs, which is now included. The local package was explicitly a smoke artifact,
not used for guest acceptance; only the exact Woodpecker archive will be handed
to the coordinator.

## First exact guest falsifier and correction

Feature `a66308b78846bac4250e091bf7f54dbebc3b448a`, Woodpecker #129, passed
all three workflows. Probe archive SHA256:
`950e5c31fb56c8fb113c1bb2ab4491d9a42211ba57477793478b6ec4f3aade2e`.
The coordinator's run `run-ydc_zxsz` produced 12 PASS and one FAIL, with successful
fresh evidence writing: file-as-directory returned native `fnfErr` (-43), while
the test incorrectly expected `dirNFErr` (-120). No successful acceptance was
claimed. Result SHA256:
`ac3a330ccd3f15bf26a725552ba5189cfee229ec383a1703c3b278c24e68e20b`;
`automation-20260907-194550/failure.png` SHA256:
`55f71affe2dbfd7996ca79d698612de4c6dec663c947ed7d31b766f7ca202044`.
The coordinator verified Return closure, normal Finder shutdown, PID exit,
closed handles for all three disks, and the unchanged original fixture hash
`072129060a85379fb701ec07988dcfb900998c6541f4a5315dba6f0d54f6835f`.

The source/doc explanation is in `PROVENANCE.md`: negative PBGetCatInfo index
selects directory IDs only; our interface does not perform MoreFiles's original
named-file type check. The corrected assertion first verifies Eight is a regular
file by name, independently submits its CNID as a negative-index native query,
requires the observed System 7 `fnfErr`, then requires the wrapper to preserve
that exact error with zero callbacks. The wrapper implementation is unchanged.
This corrects the test's path-vs-ID assumption; it does not whitelist errors.

That run also exposed Finder's desktop-rebuild warning on protected media without
Desktop Manager files. The coordinator dismissed it explicitly before launching
the probe. A bounded host-only adaptation of the pinned GPL cdrtools initializer
now builds invisible Desktop DB/DF metadata and includes both in fixture/guest
expectations. Two independent builds produced identical new fixture bytes:
`5adc9b2bb9cb9d1d7119ad45dc2d8470e221aee776a6b653d9e788e445e56f58`.
The original seven object IDs remain 16–22 (six root entries plus nested
Sentinel); desktop
metadata IDs are 23/24. New exact CI and guest must establish warning-free boot,
all 13 checks, successful evidence writing and unchanged protected image hash.

## Exact feature acceptance and integration staging

The coordinator completed exact feature
`1365553d5fed56d87580c84553983a630f69a882`, Woodpecker #132, in System 7
run `run-z2e5662v`. The newly written `morefiles-result.txt` names that commit,
contains all 13 PASS records and `ALL PASS`, and reports eight root entries.
The coordinator visually inspected `automation-20260907-230323/probe.png`,
including successful Unix evidence writing, then observed normal guest shutdown
and slot release. The entire protected fixture still matches its published hash.
An integration worker independently read the transcript and recomputed:

- Probe archive: `978b4feaeb1fd25c2ede4a60d5da6e58e1a41f3097e9625bd5c6683f0890a255`
- Result: `c49fc98c4ab57509c88eb646c4d442afd82eb0d14982716516e5aa16805991db`
- Probe screenshot: `7d34d942db25afbfa244e5f61d081922be44a17ffaeab77f7406f85b2cea8122`
- Fixture: `5adc9b2bb9cb9d1d7119ad45dc2d8470e221aee776a6b653d9e788e445e56f58`

This session resumed a previously locked staged guest. Its 27.49-second runner
measurement is **not cold-boot timing**. No dialog was visible when the
coordinator resumed, but the complete boot was not witnessed; this run does not
establish warning-free boot. The runner's generic attachment event mentions an
acknowledged desktop warning; that label is not evidence that a dialog was seen
or dismissed during this resume. Catalog functionality and successful evidence
writing are accepted for this exact feature; a fresh observed boot remains
necessary to settle the Desktop Manager warning claim.

Staging branch `work/REUSE-03-integration` starts at reviewed root integration
`592ae410770ae10dbe8c9707b64ab6a1daf220a9` and merges the feature without conflicts.
The branch adds only this acceptance account beyond the feature merge. Exact
combined-commit CI must publish separate ordinary CannedBSD and MoreFiles probe
artifacts. Both need coordinator-owned acceptance on the combined commit; the
feature result above is not substituted for those pending integration results.
