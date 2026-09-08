# MAC-02: preserve System 7 stack-sniffer state across private stacks

- Status: implemented; verification in progress
- Base SHA: `78a1e5b3a83ee8d70d4b7cf568df2419415af5c5`
- Branch: `work/MAC-02`
- Worktree: sibling `bsdinacan-MAC-02`
- Hypothesis: the VBL stack sniffer intermittently sees A7 in a heap-backed
  coroutine stack and reports error 28. Root-only `WaitNextEvent` is insufficient
  because the sniffer also runs asynchronously. The coordinator reproduced
  first-launch error 28 in two of three fresh guest clones using the previous
  exact PENV-04 artifact. The hypothesis awaits exact-artifact guest validation.

## Red

- Existing guest behavior: first launch can fail with System 7 error 28 before
  the first redraw, while repeated launches can pass.
- The native root-dispatch test places the caller on a real alternate stack and
  requires each service callback to execute on the original stack. Running the
  former direct-callback behavior (a scratch source copy that invokes the callback
  directly) failed `dispatch.active == &root` in `service`, exit 139 on the Linux
  agent image. The test calls the real dispatcher for the green run.
- The guest context check now leaves each private stack active across eight
  VBL ticks, checks that its stack-sniffer state is disabled, exercises
  allocation/resize/release, and checks restoration on return to the root.

- Additional red command: `python3 platform/mac68k/check_code_resources.py`
  against the #42 resource fork failed with CODE IDs `[0,1,2,3,4,5,6,7,8]`.
  Lazy segment loading would itself call Toolbox routines on the active stack.

## Green

- Focused command: `LDLIBS=-lucontext tests/test_mac_root_dispatch.sh` on Linux — passed.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in the existing
  Linux Woodpecker agent image on biggie — passed (optimized, sanitizer, build-mode,
  architecture, publication, and static-analyzer gates).
- Linux and mac68k Woodpecker: both passed pipeline #42 on intermediate commit
  `640b28502a3503338437f3002c4b86e6d22c5f2d`. The follow-up single-segment
  artifact is pending both checks; #42 is not evidence for that final artifact.
- Guest acceptance: required; coordinator owns the serialized emulator slot.
  No guest result has been claimed for this change.

## Change and review

- The assembly context boundary saves `StkLowPt` with A7, disables sniffing
  while A7 changes, then restores the destination's state. Newly zeroed private
  contexts start with sniffing disabled. Root returns restore normal checking.
- Toolbox allocation, resizing, release, clock reads, and fatal handling use a
  synchronous return to the original stack. The dispatcher resumes the same
  internal task immediately; it does not publish a scheduler yield. Display
  invalidation is deferred until the root event pump.
- A native test uses real separate stacks to prove callbacks execute on the
  root, deliver results to the child, and preserve uninterrupted child progress.
  The guest test additionally covers real 68K registers and VBL interrupts.
- Retro68's `--mac-single` removes hidden lazy `LoadSeg` calls from task stacks.
  CI parses the emitted resource fork and requires only CODE 0 and CODE 1.
- Public ABI, task ownership, application partition size, and kernel scheduling
  are unchanged. Existing host memory checks are retained. Assembly offset 4
  matches the new saved-stack-low field beside saved A7.

## Primary API evidence

Apple's [Inside Macintosh: Processes, Vertical Retrace Manager, page 4-5](https://dev.os9.ca/techpubs/mac/Processes/Processes-76.html)
places stack-sniffer execution among the work performed on each VBL interrupt.

Apple's [develop 16, Macintosh Q&A, December 1993, page 129](https://vintageapple.org/develop/pdf/develop-16_9312_December_1993.pdf)
answers the exact case of application-managed subtask stacks inside the heap.
It says: “storing four bytes of $00 in the low-memory global StkLowPt ($110)
will turn the sniffer off.” It also requires saving/restoring StkLowPt and A7
and warns against Toolbox calls while using the private stack. The implementation
follows both constraints rather than suppressing the sniffer globally.

[Retro68's Elf2Mac option parser](https://github.com/autc04/Retro68/blob/master/Elf2Mac/Elf2Mac.cc)
implements `--mac-single` by disabling segmentation. Its
[segment loader](https://github.com/autc04/Retro68/blob/master/libretro/MultiSegApp.c)
uses Resource Manager calls while loading segments, which is why explicit
root dispatch alone is insufficient when code is loaded lazily.

Remaining risk: native dispatcher tests cannot prove 68K interrupt behavior.
Only repeated cold launches of the exact CI artifact, fresh guest evidence,
all startup cases, and interactive shell smoke can close the guest gate.
