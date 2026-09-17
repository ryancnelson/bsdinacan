# Working on cannedBSD

This repository is organized for small, evidence-driven Iterate Bot loops. An
agent may be given only this instruction:

> Get oriented, pick a task from the backlog, create a Git worktree, and
> implement and test it.

Follow the procedure below. `BACKLOG.md` is the only task queue;
`CAPABILITY-MAP.md` is an inventory, not permission to invent a larger task.

## Get oriented

1. Find the repository root with `git rev-parse --show-toplevel`.
2. Read `AGENTS.md`, `CURRENT-STATE.md`, `BACKLOG.md`, and the relevant part of
   `SPEC.md`. Read `LIBC.md` and `UPSTREAM.md` for libc or imported-source work;
   read `TARGETS.md` and the platform README for host-port work.
3. Run `git fetch origin`, then inspect `git status`, `git worktree list`,
   `git branch --all`, and `git log -1 --oneline origin/main`.
4. Do not edit an existing checkout. Preserve every dirty file and every other
   agent's branch or worktree.

## Pick and claim one task

- If the coordinating prompt assigns a backlog ID, take only that ID.
- Otherwise choose the first `Ready` entry whose dependencies are `Done` and
  which has no `work/<ID>` branch or worktree.
- When several agents are launched together, the coordinator must assign a
  distinct ID to each one. Agents must not race to update a shared claim file.
- A task's `Base` is authoritative. `main` means the freshly fetched
  `origin/main`, never the current checkout's `HEAD`.
- Use the exact branch name `work/<ID>` so a second local worker fails to claim
  the same ID. If it already exists, leave it untouched and choose the next
  ready item.

Create a sibling worktree, substituting the backlog ID and repository name:

```sh
git worktree add -b work/<ID> ../<repo>-<ID> origin/main
cd ../<repo>-<ID>
```

Do not merge, rebase, delete branches, remove worktrees, or push to `main`
unless the coordinating prompt explicitly asks for it.

Nobody works in the primary checkout, including the coordinator: a branch
checkout there mutates the same files another agent is compiling. The symptom
is an intermittent `cannot find build/*.o` link failure, which looks like disk
trouble and is really a concurrent checkout swapping the tree under a running
compiler. Verify by read-only inspection or a throwaway clone instead.

A worktree's `.git` is a *file* pointing at the parent repository, not a
directory. Bind-mounting only the worktree into the Linux container therefore
leaves every `git` invocation failing with `fatal: not a git repository`, which
fails `check-publication` and so fails the whole `ci` gate for a reason that
has nothing to do with the change under test. Either mount the parent
repository at its real path as well, or run the gate from a disposable full
clone. Confirmed 2026-09-17 against `check-publication`.

## Run one falsifiable loop

1. Establish the task's clean baseline. On supported Linux hosts run
   `make clean test`; elsewhere use the documented Linux container or
   Woodpecker rather than weakening warnings.
2. State one falsifiable hypothesis. Add the smallest focused test that fails
   for the expected missing behavior, and record the command and exact red
   failure in `notes/iterations/<ID>.md` using the template in that directory.
   A setup error or unrelated compiler failure is not useful red evidence.
   A deliberately reverted fix can validate a regression test, but must not be
   described as a test written before implementation.

   **Every control needs a negative control: a control you have never watched
   fire is not a control.** If you add a check, a guard, a gate or an assertion,
   also break the thing it protects on purpose and confirm the check reports it,
   naming the right subject. This is distinct from red-then-green on the feature:
   it asks whether the *detector* works, not whether the behavior does. The
   discipline arrived independently five times in one day, which is why it is
   written here rather than rediscovered a sixth time:

   - A handshake harness deliberately leaked a descriptor the controller should
     have closed, and asserted the resulting deadlock was *detected and
     attributed* rather than hanging. Three earlier rounds of `rc=124` had taught
     nobody anything precisely because nothing reported which role was stuck.
   - `MILESTONE-E2E-01` unregistered one command and observed status 0 become
     127, proving the session test notices a missing command at all.
   - `STATICS-REPRO-01` reproduced each state-corruption bug under the sanitizer
     first, so the fix had demonstrated failures to verify against instead of a
     structural argument.
   - `BUILD-SYNC-01` broke each of its three parity invariants in turn and
     confirmed each named the correct source and flag.
   - `SOLARIS-02`'s marker corroboration is required to ship with a run where the
     marker is emitted early but the record count or shutdown evidence is wrong,
     so that a marker alone cannot pass.

   The failure this prevents is the one that recurs most here: a gate that is
   green because it checks nothing. `mac68k` passed for months while not
   compiling five registered commands; a damage-transport oracle passed 14 of 14
   of its own tests while falsely reporting a full match on a 1-slot geometry.
   Neither was caught by adding more passing cases.
3. Make the smallest coherent implementation pass that test. Keep refactoring
   inside the tested boundary and keep the test green.
4. Run the focused test, then the complete `make ci` gate. Assert setup,
   boot and child/probe exit results; a harness that discards a failed probe
   has not passed. Lifecycle tests must observe cleanup before a later
   exit/reap/teardown can hide its absence.
5. Inspect `git diff --check`, `git diff`, and `git status`. Review ABI version
   and structure-size handling, task ownership, cleanup, host-OS leakage,
   overflow, and tests that assert implementation details instead of behavior.
6. Commit one behavior change and push the feature branch. Wait for the
   Linux `ci`, `mac68k`, and applicable `mac-automation` Woodpecker workflows
   on the exact commit. See `notes/CI.md` for actual step logs. A local
   build is not Woodpecker evidence.
7. For every runtime, libc, VFS, shell, command, or `platform/mac68k` behavior
   change, test the exact `mac68k` Woodpecker artifact in the shared Basilisk II
   System 7 guest. Verify `SHA256SUMS`, stage fresh empty evidence before boot,
   require the complete newly written expected transcript including `ALL PASS`, and record the commit, checksum, and guest result
   in the iteration note. Only one worker may control the guest at a time;
   the coordinator assigns that serialized slot. Use the checked-in Hammerspoon runner and staging
   protocol; see `platform/mac68k/automation/README.md`. Documentation-only and
   Linux-host-only changes may record guest acceptance as not required with a
   reason.

8. Solaris 9 SPARC is also a required portability target for runtime, libc,
   VFS, shell, command, shared ABI and host-adapter changes. Follow the transition
   and exact-commit evidence policy in `notes/CI.md`. Coordinate the shared rig
   slot; historical results and skipped runs are not passing evidence. While
   SOLARIS-01 is pending, report Solaris acceptance as pending and hand it to
   the coordinator rather than claiming full cross-platform completion.

Do not delete or disable existing tests to make a gate green. An intentional
contract change requires an explicit design decision and replacement assertions
that preserve the remaining coverage.

If a check fails, diagnose it, add or refine a reproducer when appropriate,
fix the cause, and rerun every affected check. A pushed fix is not completion;
the latest commit must have green required checks.

## Source and scope rules

- One backlog ID and one observable behavior belong in a worker branch.
- Do not add speculative libc surface. Compile the pinned target source and
  choose the smallest independent missing interface demonstrated by its actual
  diagnostics.
- An interface we have not implemented must fail in a way the caller can
  detect (`ENOSYS`, `EOPNOTSUPP`, or `NULL`) -- never return a fabricated success
  stub that silences the caller without performing the operation, and never
  return fabricated data.
- Ownership is per *symbol*, not only per utility. Work assigned per utility
  while several utilities need the same libc surface produced six independent
  duplicate implementations of shared symbols (`fts_read` errno handling,
  `strrchr`, `warnx`, `signal`, `user_from_uid`, `group_from_gid`); three were
  real bugs that passed review and the gate and merged. Before implementing any
  shared symbol, check `origin/main` and the other in-flight `work/<ID>`
  branches. Consume an existing symbol rather than rewriting it; if it lacks a
  property you need, ask its owner to add it rather than keeping a divergent
  copy; report any addition to a shared header to the coordinator. State in your
  handoff which shared symbols your branch touched -- asking for that list
  explicitly caught two further collisions within minutes.
- A capability check must name the field it is checking. `cb_libc_rmdir`'s
  `struct_size` guard used `offsetof(rename)` rather than the `rmdir` field it
  was gating, and was correct only by accident of struct layout. No gate catches
  an accidentally correct check, so the field name must be verified by reading.
- Detect compiler capabilities by probing, not by matching the compiler's name.
  `$(CC)` is plain `cc` on hosts where that driver is Clang, so a `findstring
  clang` test silently fails to fire and passes a GCC-only flag to Clang, which
  rejects it and breaks the build outright.
- Imported NetBSD files remain byte-for-byte unchanged at the pinned revision,
  retain their file license, and receive a hash and `UPSTREAM.md` entry. Put all
  renaming, compatibility, and runtime adaptation in cannedBSD-owned files.
- Ordinary command source must use the cannedBSD libc veneer and must not gain
  runtime-private headers or unprefixed host dependencies.
- The portable core must not acquire host process, filesystem, thread, signal,
  socket, or terminal calls. Those mechanisms belong behind versioned adapters.
- Append versioned ABI operations; do not reorder or silently change existing
  fields. Test old sizes and absent capabilities.
- Preserve the one-host-process model and deterministic tests. Inject failures
  and use mock adapters instead of relying on timing, the network, or a private
  machine.

## Handoff

Workers add only their own `notes/iterations/<ID>.md`; the integrator updates
the shared rollups in `BACKLOG.md` and `CURRENT-STATE.md` after merging. This
avoids predictable conflicts between parallel workers.

Report the backlog ID, base SHA, worktree, branch, commit SHA, red command and
failure, focused green command, full gate, both Woodpecker results, Mac guest
artifact checksum and result when required, changed documentation, and any
bounded risk or follow-up. Claim only evidence actually observed on the
reported commit.
