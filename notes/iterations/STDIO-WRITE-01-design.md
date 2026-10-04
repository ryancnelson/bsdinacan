# STDIO-WRITE-01-design: bounded writable streams for uniq

- Status: design candidate; implementation is a separately assigned task.
- Base: `f4e3f2544772ad1cae7e17396e0449ee27e10f9d`.
- Branch: `work/STDIO-WRITE-01-design`.
- Scope: this note only; no runtime, header, imported source or guest changes.
- Hypothesis: accepting fopen(w) alone cannot support uniq output. Dispatch and
  ownership must identify a stream independently of its numeric descriptor.

## Observed source evidence

Read the assigned NEXT-UTIL-03 note and the base source, not host stdio. That
note records unchanged pinned uniq opening output with `fopen(path, "w")`,
printing with `fprintf(ofp, "%4d %s", repeats + 1, str)`, and omitting fclose.
It also records that uniq ignores fprintf failure: libc cannot turn this into
an honest command exit failure without a separately approved command policy.
No uniq execution or new failing runtime test is claimed here.

At this base `libc/cb_libc.c` defines the private FILE wrapper as descriptor,
eof, error, next (lines 11–16). Standard streams are immutable global
identities; their mutable flags are task-owned ABI cells. Dynamic wrappers are
allocated through the task allocator and linked through the opaque
`cb_input_state_v1.input_streams`. `find_input_stream` compares identities while
walking owned nodes before dereferencing a caller's pointer (988–1007).

`fopen` (1046) accepts r/rb only, checks the optional input-state accessor and
its list-bearing size, opens read-only, allocates, and links. Allocation failure
closes the descriptor and reports ENOMEM. `fclose` (1122) unlinks before close
and releases the wrapper even when close fails; it preserves the close errno.
Standard stdout/stderr close instead records task-local closure without closing
the underlying descriptors. This existing difference is outside the new scope.

`fprintf` (1566) rejects every dynamic stream. `format_output` and `write_all`
carry only a descriptor. `mark_stdio_error` selects standard flags by descriptor
1/2. `fwrite` (2363) also accepts only stdout/stderr, but already checks positive
short writes, zero progress, oversized callback returns, multiplication overflow
and complete-element accounting. `write_all` lacks the oversized-return check;
reuse the checked transfer behavior rather than copying that gap into new output.
`ferror`, clearerr, fileno and setbuf route dynamic streams through the current
input resolver; fflush rejects them. Adding write permission therefore requires
separating identity/status lookup from permission-to-read lookup.

Core `task_release_allocations` clears the opaque list before reclaiming heap
(core.c:583). `api_exit` calls it and closes descriptors before marking the task
zombie (1013); destruction handles early teardown too. `task_finish_exec` (741)
reclaims wrappers and resets standard flags, but closes only CLOEXEC descriptors.
**Successful exec must preserve non-CLOEXEC descriptors**, per SPEC.md. Losing
the FILE wrapper on exec is not permission to close such descriptors. Failed
exec must preserve both wrapper and descriptor. There is no flush obligation
because this subset is unbuffered.

## Proposed implementation boundary

1. Keep FILE opaque and add a private read/write access discriminator. Reuse
   the existing task-owned opaque list for all dynamic FILE wrappers; its
   historical field name `input_streams` stays ABI-compatible. Update comments
   to describe the broader ownership, without renaming/reordering ABI fields.
   No output-buffering framework or new callback table is needed.
2. Support exactly r/rb and w/wb. For w/wb use the existing mandatory raw open
   callback with `CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC`, permissions 0666,
   subject to the existing VFS semantics. Reject a, +, e and other unsupported
   modes with EINVAL. Prefer allocating the unlinked wrapper before destructive
   open so wrapper ENOMEM cannot truncate an existing file. Release it on open
   failure; link only once acquisition is complete. Preserve incoming errno on
   success. Open failure does not promise to undo filesystem side effects.
3. Resolve identity to a short-lived output reference containing descriptor and
   the selected sticky-error cell, plus standard-stream closure information.
   Dynamic lookup checks the list capability and membership before dereference;
   wrong-direction operations fail EINVAL without I/O. Metadata operations
   ferror/clearerr/fileno/setbuf(NULL) resolve identity without requiring read
   access. Do not infer stream identity from descriptor number: an opened file
   can receive fd 1 or 2 after raw close, independently of standard FILE flags.
4. Give formatted emission the resolved sink while preserving existing printf,
   puts, putchar and err/warn behavior. Share bounded checked writes with fwrite
   where practical, without rewriting the independent snprintf parser. Positive
   short writes advance once; negative results preserve callback errno; zero or
   oversized results become EIO. Only actual I/O failures set the selected
   sticky flag. Unsupported formats remain EINVAL after any preceding bytes,
   and successful output preserves incoming errno and existing flags.
5. Extend fwrite deliberately to writable dynamic streams in the same change;
   do not leave a half-usable FILE. Preserve its zero-size/count no-callback
   behavior, overflow/NULL validation order, signed transfer bounds and complete
   element count after partial failure. No replay of a partial element.
6. fflush on a live writable stream succeeds without clearing flags or claiming
   persistence; fflush(NULL) remains an unbuffered no-op under its existing
   capability contract. fclose unlinks before callbacks and invalidates the
   wrapper even on close failure, preserving existing semantics. The raw close
   layer owns descriptor-release behavior; never retry a failed close blindly.

Compatibility is per accessed field: dynamic streams require the existing
input-state accessor and list-sized state, not a newly enlarged sizeof check.
A missing callback, short API, wrong ABI, NULL state or short state returns
ENOSYS before acquisition/I/O. Standard output retains its existing old-runtime
fallback where printf/fprintf operate without optional error-state storage;
fwrite and state-dependent APIs keep their existing requirements. An added
private wrapper field needs no public structure-size extension.

The reference and counters live on the current task stack across a yielding
write. The wrapper/error storage belongs to that task, not a process-global
current-stream pointer. Another task cannot close it through libc membership
lookup. Termination reclaims it without returning to suspended code. No promise
is made for application misuse that raw-closes/reassigns a FILE's descriptor.
Likewise membership rejects foreign pointers and closed pointers absent from
the list, but cannot distinguish an old pointer after allocator address reuse.
Do not advertise universal stale-pointer detection or add a generation framework
for C use-after-fclose, whose pointer lifetime has already ended.

## Small implementation and acceptance sequence

These are acceptance slices of one separately assigned STDIO-WRITE-01, not
parallel ownership of the same symbols:

- First add a failing writable-file probe: create an absent file, truncate a
  longer file, fprintf the actual uniq count format, inspect exact bytes, close
  and verify descriptor/wrapper release. Baseline w returns NULL/EINVAL. Test
  directory/open failure, wrapper ENOMEM before truncation, and no leaked list
  node or descriptor on each acquisition failure. Preserve existing read tests.
- Add deterministic mock writes for positive short transfers, partial-then-error,
  zero progress and oversized returns; assert bytes, return counts, errno and
  sticky flags. Cover fwrite binary data, partial elements, zero requests and
  overflow. Prove clearerr and later successful writes preserve the specified
  state. Include dynamic fd 1/2 to detect accidental standard-flag coupling.
- Test invalid/foreign/closed identities before any callback, read-on-w and
  write-on-r rejection, two dynamic streams and two interleaved tasks, and a
  yielding output callback. Check independent offsets, flags and saved errno.
  Test close failure with a mock that records descriptor disposition and wrapper
  invalidation, rather than assuming a failed close leaves it usable.
- Extend existing tests/test_file.c allocation/lifecycle observations: after
  exit but before wait/reap observe NULL list and released wrappers/descriptors;
  successful exec observes NULL list, released wrappers, preserved non-CLOEXEC
  fd and closed CLOEXEC fd; failed exec leaves usable stream state. Early kernel
  teardown releases everything. Include short actual API/state allocations,
  absent accessors and wrong versions, not just forged size values on full structs.
- Integrate a portable guest probe for dynamic fprintf/fwrite and lifecycle
  behavior; run exact-commit Linux CI, mac68k, applicable automation checks,
  independent review, fresh staged Mac artifact acceptance, and required Solaris
  qualification. A local mock or build does not substitute for these gates.

## Coordination with FGETLN-01

STDIO-WRITE-01 owns FILE layout, identity resolution, fopen/fclose and shared
status dispatch until reviewed. FGETLN starts from that selected commit, adding
its own per-dynamic-stream buffer/length/capacity and task-owned stdin storage.
Do not reserve speculative fields now. Its design must append and field-guard
any new stdin ABI storage, clear core references before heap reclaim, free a
line buffer on fclose, and define borrowed-buffer invalidation on subsequent
stream input and close. A line cache is input storage, not output buffering.
The existing input/error ownership remains shared; a second global line buffer
or divergent FILE list is forbidden. ASPRINTF needs no FILE representation and
can proceed separately with coordinated declarations.

## Validation and handoff limits

This is a source-based design; no red/green execution, full local build or guest
run was necessary or performed. The commands inspected cb_libc.c, abi.h,
core.c, SPEC.md, LIBC.md and tests/test_file.c at the exact base. `git diff
--check` is the local documentation validation; feature-commit Woodpecker status
is reported separately. No shared symbol was modified. Review must settle the
bounded policy above before implementation; this note does not claim uniq runs.
