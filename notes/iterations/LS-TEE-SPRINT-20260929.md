# LS / tee sprint integration candidate

This branch combines reviewed runtime candidates for exact integration testing;
it is not accepted main and has not run in the Mac or Solaris guest.

- Base main: `897efc71a3dce8d30b43131959af4d38c4b95ab9` (documentation checkpoint,
  exact #511 all three workflows passed and independent review clean).
- LS/cache: `5ba7741d5228cf9765c1bc5e9d723582633ffca3`, exact #508 all three
  workflows passed. Independent review found root directory traversal ignored
  comparator order. Linux log inspection found three core PASS markers and no
  UBSan runtime-error or ASan ERROR markers.
- LS repair: `eb10bb5592a829090a82eabc423400973cbb0c16`, exact #513 all three
  workflows passed. Actual ls section ordering failed before repair, then normal,
  reverse, size-tie and time-order tests passed. Root independently reviewed cursor
  ownership, bounds, cleanup and assertions; no remaining blocker in that patch.
- SIG-02: `7712111f387fbf83f4deedb1d9d873e130185ad5`, exact #507 all three
  workflows passed. Independent review found rename wrongly used whole-table size.
- SIG repair: `850ae237bfbc43e4264586b59158e39287a86b07`, exact #514 all three
  workflows passed. A genuinely shortened API allocation failed rename before
  the fix and succeeds with verified filesystem mutation afterward. Independent
  root patch review clean. Sanitizer errors absent; known ASan no-return warnings
  remain, so this is not a warning-free claim.

## Merge resolutions

The LS merge preserves main's accurate pending statuses in BACKLOG.md rather
than reviving older Done claims. The SIG merge preserves both sets of Makefile
probe sources and objects. ABI extension order is rename, wall_clock_millis,
set_interrupt; core initialization supplies both new callbacks. No accepted ABI
field was reordered. These previously unmerged candidates must be rebuilt with
this shared layout; their individual artifacts cannot qualify this integration.

Build parity passed locally after resolution. Exact integrated ci/mac68k/
mac-automation results and independent integration review remain required.
Fresh guest acceptance also remains required. Ryan owns the running interactive
Mac slot; it was not altered. Host desktop was locked during this checkpoint,
preventing new app-agent submissions or guest automation. A read-only attempt
through the documented Solaris SSH route failed authentication; no VM operation
was performed. This is a blocked route, not evidence of Solaris runtime failure.

The ten-case MAC-MILESTONE-TEST-01 document is still a proposal. This integration
has not added those cases, imported tee, or claimed every ls option is supported;
retain the explicit option/limitation matrix in STATICS-CACHE-02.md. Finish ls
qualification, then import tee, then conduct the planned quality review.

Integration review caught the signal probe object missing from the Linux test
link recipe despite its retained prerequisite. The object was restored to the
link command before qualification; the initial integration is not a passing
build. Independent re-review and exact replacement CI are required.
