# Combined ls / tee / quality candidate

This branch combines current main `9814cec`, reviewed ls/signal/milestone
`f66aab3` (#518 all-three success), reviewed actual tee acceptance `218bcdf`
(#521 all-three success), and reviewed fixed-width formatting `fb8e28f`
(exact #524 pending at composition). Nothing here claims guest acceptance.
The actual tee implementation `fce35e5` also passed exact #519 and its full
local Linux gate. Current main includes fatal sanitizer policy `03176ef`
(#520) and the reviewed documentation checkpoint (#523).

## Resolutions and focused checks

The Makefile conflict retained both the fatal sanitizer policy prerequisite
and the real pinned-source printf audit. The CMake conflict retained both the
actual tee object and the attributed format checks on ls/humanize_number.
No source or ABI conflict required manual changes. All 80 transcript records
remain, with derived 2051-byte capacity including NUL.

After resolution, build parity, all 20 host guest-protocol tests, the output
capacity check, the actual pinned-source format audit and the negative/positive
sanitizer controls passed on host Clang. These checks do not execute System 7.
Final exact combined ci/mac68k/mac-automation and independent merge review are
still required before any integration decision, followed by guest qualification.

The formatting repair probes exact target typedef compatibility. Its CMake
probe currently forwards global C flags for the supported dedicated Retro68
compiler; arbitrary configuration-specific ABI/sysroot/target flags would need
additional propagation. No broader toolchain support is claimed.

## Remaining acceptance / quality boundaries

- Ryan's interactive Mac slot is reserved; the host was last verified locked.
  No guest shutdown, input, disk change, fresh staging or acceptance occurred.
- Native Solaris qualification is pending. Documented access rejected
  authentication; the coordinator requested the current trusted SSH route.
- LS option restrictions (no symlinks/whiteouts, unsupported -X, fixed console
  width, RAMFS block/ownership limits) remain in the explicit source audit.
- Compiler-inserted ASan context-switch/no-return warnings remain separate from
  UBSan/ASan errors. Fatal-UB policy prevents quietly accepting recoverable UB.
- The known cat allocation-warning truncation at unsupported %zu remains a
  measured follow-up; it is not fixed by the PRI64 type correction.
