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
