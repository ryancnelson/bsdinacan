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
