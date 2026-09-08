# ARGV-01: keep original argument ownership outside mutable argv

- Status: implementation complete; exact CI and coordinator guest acceptance pending
- Base: `894b753` (fresh main when this isolated task started)
- Branch: `work/ARGV-01`
- Hypothesis: replacing an argument pointer with an ordinary malloc result must
  neither double-free that result nor leak the original copied argument.

## Red before implementation

The first test added `tests/libc_argv_probe.c`: ordinary `main` allocates 32
bytes, copies a replacement argument, assigns `argv[1]`, and returns without
freeing the replacement or restoring the original. A fresh kernel registered
that program, booted it, checked its result and destroyed the kernel. No runtime
changes were present in this test snapshot. This follows the ownership pattern
observed in the pinned head command's obsolete-option rewrite; it does not
claim to run head itself.

In a disposable Linux x86_64 container using the existing CI agent image and
libucontext (Alpine 3.22.5, GCC 14.2.0, Clang 20.1.8), the commands were:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --argv
make CC=clang BUILD_VARIANT=argv-red \
  CFLAGS='-std=c99 -Wall -Wextra -Werror -Wpedantic -g -O1 -fsanitize=address -fno-omit-frame-pointer' \
  LDFLAGS=-fsanitize=address LDLIBS=-lucontext build/argv-red/test_core
./build/argv-red/test_core --argv
```

The normal run terminated with exit 139, `Segmentation fault (core dumped)`.
ASan then reported `AddressSanitizer: attempting double-free`, identifying the
same 32-byte replacement allocation. The preserved pre-fix source snapshot
archive SHA256 is
`1a3964ae9264ae5fe6b12de12dbec01aed0042088aaa568f0029bacd490587fc`.
The first container launch used its agent entrypoint by mistake; that setup
error is not counted as red evidence.

## Implementation and bounded ownership

Each task has a private original-string pointer ledger and a separate writable
argv vector. The exposed vector initially points at those strings. Cleanup
frees the exposed vector allocation itself and walks only the private ledger;
program edits, duplicate pointers and early NULL entries cannot redirect it.
Tracked program allocations retain their existing independent lifetime.

The same ownership pair is prepared for pending exec, destroyed on allocation
failure, transferred together on successful exec, and reclaimed at task
teardown. Startup identity still refers to the original owned argv[0] string
and changes only when exec installs the new argument storage. No public ABI
field, program registry limit, libc function or upstream source changes.
The bounded cost is one extra pointer-vector allocation per argument image.
Environment ownership is unchanged by this task.

## Executed regression boundaries

The ordinary source remains separate, compiled with private libc headers and
checked for prefixed malloc/memcpy imports. Both native and Mac registrations
call it. The host-only allocator observer quarantines released allocations
until each fresh-kernel case completes, preventing address reuse from hiding a
second release. It checks the public vector, all three original strings and
the replacement independently:

- Normal exit: replacement released once while originals remain live before
  wait; wait releases each original and vector once before parent exit.
- Early NULL and duplicate/replaced argv pointers preserve those same counts
  and the saved startup identity.
- Successful exec: originals and replacement already released exactly once
  inside the new entry, before its exit; new argv contains a distinct deep copy
  of the replacement contents and the new startup identity.
- Missing executable: ENOENT leaves originals and replacement live, followed
  by normal exit and checked wait cleanup.
- Seven injected exec-copy allocation failures cover the original ledger,
  argument strings, public vector and inherited environment. Each returns
  ENOMEM with unchanged old arguments/identity and no net temporary allocation.
- Spawn failure injection advances through all creation allocation sites until
  success; every failed attempt restores the live-allocation count.
- A yielding child is deliberately left live when the boot shell exits. Its
  originals and replacement are still unreleased before kernel destruction,
  then each is released exactly once; all observed allocations are reclaimed.

The shared `argvprobe` performs ordinary replacement with exit/wait, early
NULL, successful exec and failed exec. It adds one direct Mac record, preserving
all 42 existing records for **43 expected PASS records**. New native tests use
fresh scoped kernels; the 64-program capacity and full fixture are unchanged.

## Verification and handoff

- Focused Linux command above now reports `argv ownership tests passed`.
- `python3 -B tests/test_mac_guest.py`: all 18 tests passed with 43-record check.
- Full Linux command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed
  (normal tests, ASan/UBSan, build variants, repeated normal tests, analysis).
  The first disposable snapshot lacked Git metadata required by publication
  checks; that setup failure was repaired before running the complete gate.
- Exact pushed-commit Woodpecker ci/mac68k/mac-automation: pending.
- Exact guest artifact and fresh 43-record acceptance: pending coordinator's
  serialized guest slot. A compile or native test is not guest acceptance.
