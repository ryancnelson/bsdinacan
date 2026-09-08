# MoreFiles catalog feasibility diagnostic

**Not yet viable with the pinned Retro68 SDK.** No MoreFiles app, HFS fixture,
or successful guest result is produced by this experiment. The separate
Woodpecker step `diagnose-morefiles-not-viable` succeeds only when it reproduces
the reviewed compile blocker. This is not a successful MoreFiles compilation.
A changed result fails the diagnostic and requires review.

Run inside the same pinned image as `.woodpecker/mac68k.yml`:

```sh
python3 platform/mac68k/probes/morefiles/diagnose.py --output /tmp/morefiles-new-run
```

The output directory must not exist. The script verifies every source hash,
compiles a real `PBGetCatInfoSync` SDK control, then separately compiles the
unchanged iterator and helper translation units. It records compiler identity,
commands, statuses, diagnostics, source pins, and an explicit `viable: false`
result. It never links these inputs into CannedBSD. No CPU, SDK, calling
convention, source, or global build flag is changed.

The SDK control compiles. `IterateDirectory.c` fails in `MoreFilesExtras.h`
on missing `XVolumeParam` and other Universal Interfaces declarations;
`MoreFilesExtras.c` fails at missing `Finder.h`. The pinned image supplies
Multiversal headers, and no alternative `Finder.h` was found under its
Retro68/toolchain trees. Dead-section elimination cannot repair missing types
while compiling the larger translation unit.

The CI wrapper archives its result under a distinct
`/artifacts/PIPELINE-COMMIT/morefiles-diagnostic.*/MoreFiles-diagnostic.tar.gz`,
with commit, pipeline and SHA256SUMS. The normal `CannedBSD.tar.gz` artifact and
its build/acceptance process remain separate. If source checking, compiler
setup, SDK control, or the expected outcome fails, the CI step fails.

Next scope decision: either provide a licensed, pinned Universal Interfaces
SDK compatible with this toolchain, or separately review an explicitly altered
minimal MoreFiles helper extraction and the small header declarations it needs.
The second approach must preserve notices, original hashes, actual Toolbox
layouts and callback ABI; it must not invent records or import unrelated AFP,
large-volume or Desktop Manager behavior. Only a successful real compile/link
can proceed to the protected HFS fixture and exact-artifact guest checks in
`notes/iterations/REUSE-02-plan.md` (reviewed commit `dd88ee7`).
