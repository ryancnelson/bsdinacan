# REUSE-02 plan: MoreFiles read-only catalog viability probe

This proposes **one worker item**, not an implementation or a new host mount.
It follows [REUSE-01](REUSE-01.md). The coordinator requested this note in the
existing `work/REUSE-01` worktree; no shared backlog/status file is changed.
The queue was checked at `origin/main` `52cc6f9`. REUSE-01's integration
candidate `ca0cd19` was reported by the coordinator but was not yet main.

## Proposed backlog entry

### REUSE-02 — compile and exercise a MoreFiles read-only catalog probe

- **Status:** Proposed for the coordinator to mark Ready after REUSE-01 lands.
- **Base:** freshly fetched `origin/main` containing the REUSE-01 source audit.
- **Depends on:** REUSE-01; access to the existing pinned Retro68 Woodpecker
  workflow and a coordinator-assigned System 7 guest slot. No dependency on
  VFS-03 implementation: this is a host-library feasibility probe.
- **Hypothesis:** the pinned MoreFiles directory/catalog subset can be compiled
  by our existing 68000-compatible Retro68 toolchain and can read a known HFS
  fixture through an ordinary C callback on the original Mac application stack,
  without introducing another scheduler, libc, descriptor table or VFS model.
- **Falsifier:** a source/license provenance gap, an unresolved compiler/ABI
  dependency outside the bounded subset, incorrect catalog/callback results,
  fixture mutation, or guest corruption prevents a positive viability finding.
- **Accept:** verified source pins and notices; reproducible cross-build of the
  real MoreFiles functions; exact-CI-artifact guest evidence for the cases below;
  clean exit/shutdown; an explicit viable/not-yet-viable recommendation with
  remaining adapter work. No claim that a cannedBSD host mount now exists.

## Smallest source boundary

Use the official MacPerl CVS snapshot pinned in REUSE-01:

- URL: <https://sourceforge.net/code-snapshots/cvs/m/ma/macperl.zip>
- Archive SHA256:
  `afc00b6009d79d37b7146116e6ce1a44a4f530303b693cb217c9de0604bd6222`.
- Module/path: `perl/macos/ext/Mac/MoreFiles/MoreFilesSrc/`.
- Candidate entry points: `IterateDirectory` and `FSpIterateDirectory` from
  `IterateDirectory.c`/`.h`, CVS revision 1.1. Use `maxLevels=1`; the upstream
  header explicitly specifies one-directory traversal for that value.
- Actual immediate implementation dependencies: `GetDirectoryID` and
  `DetermineVRefNum` in `MoreFilesExtras.c`. They in turn use
  `GetCatInfoNoName` and `GetVolumeInfoNoName`; inspect the complete reachable
  chain rather than replacing those calls with guessed stubs.
- Candidate compilation/header set: `IterateDirectory.c/.h`,
  `MoreFilesExtras.c/.h`, `MoreFiles.h`, `MoreDesktopMgr.h`, `FSpCompat.h`,
  `Optimization.h`, and `OptimizationEnd.h`, all observed as revision 1.1 in
  this snapshot. Include additional implementation units only when the real
  compiler/linker demonstrates a dependency. `FSpCompat.c` is a candidate, not
  an automatic requirement merely because its header is included.

For each selected file, compare revision and SHA256 with the audit (or add a
pin for an unlisted dependency), retain its exact bytes and notice, and record
source URL/path/revision/hash in `UPSTREAM.md`. Put compiler adaptations in
cannedBSD-owned headers or build flags; if an upstream modification proves
necessary, record the original hash and a plainly identified alteration.
Do not silently rewrite the imported code or import the whole MacPerl tree.

The iterator carries the Jim Luther/Apple 1995–1999 notice permitting
incorporation without restriction and assigning operation responsibility to
the user. The other examined MoreFiles helpers/optimization headers carry the
Apple sample-code notice requiring altered redistributed source to identify
its origin/changes. Verify each newly selected header's actual notice too;
neither the Perl Artistic/GPL grant nor GUSI's license is a replacement for
these notices. Missing/contradictory provenance blocks that file's import and
must be reported, rather than treated as a successful reuse result.

## Build experiment and falsification

Add a small, separately named Mac probe application/target under
`platform/mac68k/probes/`; keep it out of the normal cannedBSD runtime path.
Use the existing pinned Retro68 image and CPU/link conventions. The worker may
add an explicit probe build step/artifact to `mac68k`, but must preserve its
ordinary CannedBSD artifact and all existing gates. No general build-system
or acceptance-runner rewrite belongs in this item.

First attempt to compile the selected source unchanged and save the real
compiler/linker output. The result may be a successful characterization;
**do not invent a failing test simply to manufacture red evidence**. If it
fails, reduce it to the actual entry point/dependency and record the diagnostic
before applying a bounded compatibility shim. Missing headers, unsupported
compiler directives and ABI mismatches are distinct findings.

Inspect and test, rather than guess, these known risks:

- `pascal` calling conventions on the iterator and `IterateFilterProcPtr`;
- Toolbox `CInfoPBRec`/FSSpec layouts and structure alignment;
- old compiler pragmas and `Optimization.h` platform assumptions;
- dependencies dragged in by the larger `MoreFilesExtras.c` translation unit.

The callback is an ordinary function-pointer call in this upstream iterator;
do not conflate it with GUSI's asynchronous A0 completion wrappers. Keep
Toolbox work on the original application stack regardless. If using a
MoreFiles minimum-System macro, state/check that guest requirement before
calling it. Do not enable an unsupported CPU instruction set, change global
ABI rules, add empty library stubs, or suppress broad diagnostics to obtain a
link. Existing function-section/dead-section elimination can discard unused
helpers, but cannot replace a missing reachable implementation.

If a minimal adapter requires a new C runtime, GUSI scheduler, wholesale
compiler emulation or an unreviewed calling-convention change, stop expansion
and report **not yet viable within this slice**, with the first concrete
blocker and a proposed separate follow-up. A failed experiment is useful
backlog evidence; it is not a completed reusable host backend.

## Fixture and exact guest checks

Build a small deterministic **real HFS fixture image**, independently from the
emulator's Unix/extfs share. Archive its expected manifest and SHA256 beside
the probe artifact. Do not infer HFS behavior from extfs alone. Suggested root
contents are an empty directory, a subdirectory with a nested sentinel, a
zero-byte file, an eight-byte data file (`fixture\n`), a file with known
nonzero data and resource-fork lengths, and a 31-byte ASCII filename. Record
exact names, types and fork lengths in the fixture manifest. The probe receives
the chosen mounted volume identity explicitly; use volume reference/directory
ID for subsequent calls rather than a process-global `chdir`.

The host stages a fresh run, the exact probe artifact, protected read-only
fixture, and separate precreated evidence files before boot. The coordinator
owns guest input. Require these observable assertions:

1. Enumerate the root with `maxLevels=1`; compare the unordered entry set to the
   manifest. The nested sentinel must not appear. Empty-directory enumeration
   succeeds with zero callbacks. Do not promise undocumented enumeration order.
2. Check catalog type, data/resource-fork lengths and returned file/directory
   IDs. IDs must be nonzero, distinct for distinct fixture objects, and stable
   over two scans of the same mounted fixture. Match pre-recorded IDs too if
   the deterministic fixture builder supplies them; do not compare host-inode
   numbers or require IDs to survive rebuilding the HFS image.
3. Use separate caller-owned result buffers for two scans and prove one cannot
   overwrite the other's names or metadata. Copy callback data immediately:
   upstream explicitly makes its catalog record read-only and temporary.
   This is repeatable callback traversal, **not** proof of a resumable,
   independently interleaved `DIR` cursor contract.
4. Set `quitFlag` after the first callback, verify bounded early termination,
   then verify a fresh full scan still returns the complete set. A callback
   canary/count catches a calling-convention or argument-layout failure.
5. Test a missing child directory, passing a regular file as the directory,
   and a null callback. Record native `OSErr` values; the inspected source
   returns `dirNFErr` for a regular file and `paramErr` for a null callback.
   Resolve the missing-name expectation from the called File Manager API and
   fixture path; do not hide unexpected errors with a broad success whitelist.
6. The fixture remains unchanged. Verify read-only protection before boot and
   its entire image SHA256 again after verified clean guest shutdown. Result
   evidence goes to the separate run share, never onto the fixture. If host or
   emulator mount bookkeeping alters the image, fix the fixture protection
   before crediting this check; distinguish that from a source-library defect.

The probe should report every case and its actual native error/counts, with a
single terminal success/failure result. Keep the screenshot and freshly written
machine-readable output. A visible success with an evidence-write failure does
not pass. Host records bind exact feature commit, Woodpecker run, probe archive
hash, fixture hash and output hash. A compile alone, a result from a prior run,
or the ordinary CannedBSD startup pass is insufficient evidence for MoreFiles.
Use normal app exit and inspect any shutdown confirmation; verify the emulator
has exited and disks are closed before hashing/releasing the serialized slot.
Manual launch/inspection is acceptable; a new automation framework is not part
of this probe.

## Boundaries and handoff

- Do not change `src/vfs.c`, RAMFS, task cwd/root, libc `dirent`, descriptor
  inheritance, errno ownership, or polling. VFS-03 and IO-01 remain independent
  workers' contracts.
- Do not expose a `/Host` mount yet. This fixture probe does not establish
  subtree confinement, alias policy, mutable-directory semantics, writeback,
  permission translation, host removal races or persistent cursor lifetimes.
- Do not import GUSI, replace the current Mac console, add networking, or use
  Thread Manager threads for the probe. Preserve the root-stack Toolbox rule.
- Handoff includes the final minimal source/dependency list and licenses,
  compile diagnostics, code-size impact, exact CI statuses, all guest evidence,
  and a viable/not-yet-viable conclusion. A positive result can unblock a
  separately specified host-volume adapter after VFS-03's contract is ready.

## Checks for this planning-only commit

Read the source audit, current backlog and the upstream iterator/header/helper
call chain. Verified this note's proposed file revisions against the downloaded
CVS checkout; `git diff --check` passes. No source import, probe compilation or
UI action was performed. Runtime/guest tests are not applicable to this plan;
its eventual implementation must supply the evidence specified above.
