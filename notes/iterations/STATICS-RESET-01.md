# STATICS-RESET-01

## Problem

Pinned commands' cross-invocation state (file-scope statics, function-scope
statics, and plain non-static globals) is never reset between invocations in
a runtime that runs each invocation as a task inside one host process and
frees that task's allocations at exit. Real BSD source assumes a fresh,
zeroed process image every time; this runtime never gave that assumption
back until this ID.

TEE-STATE-01-design.md already identified the identical hazard for tee.c's
`head` and built a bespoke per-task executor-wrapper fix for it (never
landed, since no real tee import exists). This ID generalizes that pattern
into one reusable wrapper (`src/static_reset.c`) shared by ls/cat/mv/rm/cp,
rather than writing a fourth or fifth bespoke variant.

## Mechanism

Full design rationale, including the two sub-mechanisms and their
trade-offs, lives in `src/static_reset.c`'s own top-of-file comment — this
is the canonical reference, not a duplicate of it. Summary:

1. **File-scope statics and plain non-static globals** (same slot-list
   mechanism, since a non-static global already has real external linkage
   and needs no rename): `objcopy --redefine-sym old=new
   --globalize-symbol=new` promotes a file-scope static's compiled symbol
   to global under a new name; a non-static global is `extern`-declared
   directly. A generic executor-ops wrapper (`cb_static_reset_ops`)
   memcpy-restores each declared slot from a saved buffer before
   delegating to the native executor, saves the live value back after
   (unless the task exited), then always clears the live global before
   returning to the scheduler. A per-program `static_defaults` buffer,
   captured once from each slot's live value before any task of that
   program has ever run, seeds a new task's own saved buffer — needed
   because a blind zero-fill is wrong for a slot whose real compile-time
   default isn't zero (ls.c's `termwidth`, default 80).

2. **Function-scope static pointer caches** (print.c's `array`, mv's `bp`,
   cat's `buf`): not reachable this way — a function-scope static's
   compiled symbol name is compiler-internal and, under GCC, mangled with
   a non-deterministic numeric suffix, not portable to rename (this
   matters concretely: mac68k's Retro68 cross-toolchain is GCC-based).
   Fixed instead via `CB_EXECUTOR_PERSISTENT_HEAP`: a task's own
   allocations are spliced onto the shared program's list instead of
   freed at task exit, so the pointer never dangles. This only works
   because each of the three actual cases fits one of two shapes: the
   command's own logic re-derives what it needs regardless of staleness
   (ls, mv), or the pointer's own gate variable is itself left unmanaged
   so gate and cache move in lockstep (cat). See "Storage-class coverage"
   below for what this does NOT solve.

## Storage-class coverage (stated explicitly, per direct request)

- **File-scope static**: covered.
- **Plain non-static global**: covered, more easily than file-scope
  (no rename needed).
- **Function-scope static**: covered *only* for the two shapes above. A
  function-scope static whose own correctness requires it to reset to
  NULL/zero between invocations, with no unmanaged partner variable to
  pair it with, is not something this mechanism can fix. That would need
  either editing pinned source (forbidden) or per-task writable-data
  copies / per-command linker sections — a materially larger, separate
  piece of work, not a reset-list extension.

## Bugs found and fixed

Five demonstrated (via `tests/test_statics_repro.sh`, adapted from
antigravity's STATICS-REPRO-01 red suite at commit `951b11b` on
`work/STATICS-REPRO-01`) plus two found independently during this ID's own
implementation:

1. **ls printcol() UAF** (demonstrated) — a second `ls` whose entry count
   doesn't exceed an earlier one's bypasses `realloc` and writes through
   memory the first ls task's own exit already freed. Fixed by
   `CB_EXECUTOR_PERSISTENT_HEAP`. This is the bug LS-02 was held for.
2. **cat raw_cat() UAF** (demonstrated, `-B` only) — reachable only when
   `-B` requests a buffer larger than the built-in 1024-byte `fb_buf`
   (`cb_libc.c` hardcodes `st_blksize=1024`, equal to `BUFSIZ`, so the
   fstat-driven default path never mallocs). Fixed the same way as ls.
   A *separate* bug was found independently while building the red test
   for this ID: an earlier version of this file reset `bsize` (file-scope,
   objcopy-reachable) alone while leaving `buf` (function-scope,
   unreachable) alone, desyncing the pair — `buf` stayed non-NULL from a
   prior invocation, `raw_cat()`'s own re-initialization gate never fired,
   and a second invocation ran `read(rfd, buf, 0)`, which a pipe's read()
   treats as immediate EOF. Fixed by leaving the whole `bsize`/`buf` pair
   unmanaged, matching mv's already-correct `bp`/`blen` pattern.
3. **rm eval stale exit status** (demonstrated) — a failing rm followed by
   a successful one in the same session exits 1. Fixed by resetting
   `cb_rm_eval` every invocation.
4. **cp flag leakage** (demonstrated, worse than the original `-v`-only
   framing) — cp.c's own `main()` already resets 4 of 11 option flags
   (`Hflag`/`Lflag`/`Pflag`/`Rflag`); the other 7 (`fflag`, `iflag`,
   `lflag`, `pflag`, `rflag`, `vflag`, `Nflag`) leak. `-f`/`-i`/`-l`/`-p`
   change cp's semantics, not just its output — `-v`'s leak is merely
   cosmetic. Fixed by resetting all 11 externally, moot with respect to
   cp.c's own partial internal reset.
5. **mv fastcopy() UAF** (latent, NOT demonstrated) — identical shape to
   ls's and cat's, confirmed directly against the source, but every
   session directory (`/`, `/tmp`, `/home`, `/bin`) sits on the single
   unified root ramfs mount, `rename()` never returns `EXDEV` inside one
   mount, and `fastcopy()` is only reached on that fallback path — no
   shell command exercises it today. Fixed anyway (`CB_EXECUTOR_PERSISTENT_HEAP`)
   for whenever a second mount makes it reachable.
6. **ls's other 32 cross-invocation globals** (found independently, not
   part of either audit) — the wrapper originally covered only `output`
   (the LS-02 header leak); `blocksize`, `termwidth`, `sortkey`, `rval`,
   and 28 `f_*` option flags were completely unmanaged. A session mixing
   e.g. `ls -l` then a plain `ls` would carry `f_longform`/`rval` across.
   Fixed by adding all 32 to `ls_slots`.
7. **cp's `dnesp`** (flagged by audit, NOT reproduced by the follow-up
   repro suite) — `pushdne()`/`popdne()`'s recursion-depth index into
   `dnestack`. A stale nonzero value from an aborted `cp -r` could
   plausibly corrupt a second `cp -r`'s recursion bookkeeping, but this
   is explicitly *not* claimed as a verified fix the way 1–6 are — the
   user was explicit it remains an unproven hazard from the audit. Fixed
   anyway, at zero marginal cost (same file-scope-static/objcopy
   mechanism as every other slot here).

Also fixed along the way: `ls_interleave_child_program` in `tests/test_core.c`
was registered under the plain native executor, bypassing
`CB_EXECUTOR_PERSISTENT_HEAP` entirely and reproducing the exact original
UAF under the sanitizer once the test's own `-1` workaround was removed —
found by running the red test, not guessed.

## Verification

`tests/test_statics_repro.sh` (inverted from antigravity's original
pass-means-reproduced sense to pass-means-fixed) runs all five cases
against `build/sanitize/bsdinacan` as part of `make test`, under every
build variant `make ci` exercises. All five: fixed (case 5 confirms
correct behavior only; it cannot exercise the latent UAF without a second
mount).

## Open items

- mac68k CMakeLists.txt has not been updated for this ID. Whether Retro68's
  GCC-based cross-toolchain provides `objcopy` in a form `CMAKE_OBJCOPY`
  exposes is an open risk, not yet investigated — central to this whole
  mechanism, so mac68k parity is required, not optional.
- cp's `dnesp` fix is unverified (see above) — a real repro, if one
  surfaces, would need a `cp -r` that errors out mid-recursion without
  reaching a balanced pop depth.
