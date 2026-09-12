# SIG-01-matrix

## Base Evidence & Scope
- **Base Commit:** `adf62f122a675706f0df7681b6cfb0ee2f039062` (accepted Solaris main)
- **Goal:** Derive concrete unexecuted acceptance cases for the cooperative interrupt delivery design documented in `notes/iterations/SIG-01-design.md`.
- **Scope:** Documentation only. No runtime implementation, no test rig usage, and no rollup edits. Solaris integration is accepted. Signal support remains a prerequisite for tee; formatting for uniq is a later utility task.

## Identified Symbols & Proposed Bounds

- **Existing Symbols:** `api_exec`, `cb_task_yield_as`, `cb_kernel_register`, `task_finish_exec`, `CB_TASK_EXEC_PENDING`, `CB_TASK_RUNNABLE`, `waitpid` (project convention returns raw exit code).
- **Proposed Additions (No invented names):**
  - An append-only `cb_api_v1` signal setter operation utilizing explicit integer disposition values for default/ignore. The private signal() veneer maps pointer markers to those integers; it never casts arbitrary integers to function pointers.
  - An append-only capability word on `cb_executor_ops` defining a cooperative-interrupt-delivery bit, guarded by an explicit size check against the original prefix.
  - An internal queue/request function taking a kernel and internal PID, acting as the real request boundary.
  - Task-owned scalar fields for disposition and a coalesced pending interrupt bit.

## Acceptance Matrix (Unexecuted Cases)

### 1. Default / Ignore States
- **Default:** A task with `SIG_DFL` blocked on a pipe receives an internal interrupt request.
  - **Expected:** Task state becomes `RUNNABLE`. Delivery triggers immediately upon resumption via `cb_task_yield_as`. The task executes its own cleanup and exits with status `130`. `waitpid` observes `130`.
- **Ignore:** A task with `SIG_IGN` blocked on a pipe receives a request.
  - **Expected:** Request is quietly discarded. The task remains blocked on the pipe until valid input arrives.

### 2. Pending Coalescing
- **Action:** A task with `SIG_DFL` receives multiple identical internal interrupt requests before it next yields.
  - **Expected:** The pending interrupt bit is set once (coalesced). Upon delivery at the next safe boundary, the task cleans up and terminates exactly once. No duplicate destruction paths are invoked.

### 3. Spawn Inheritance Without Pending
- **Action:** A parent task with `SIG_IGN` spawns a child.
  - **Expected:** The child inherits `SIG_IGN`.
- **Action:** A parent task with `SIG_DFL` and an active pending interrupt bit spawns a child.
  - **Expected:** The child inherits `SIG_DFL` but **never** inherits the pending bit. The child runs cleanly; the parent terminates with `130` upon its own next boundary.

### 4. Successful / Failed Exec
- **Successful Exec:** A task with `SIG_IGN` successfully commits a new image via `api_exec`.
  - **Expected:** The new image retains the `SIG_IGN` disposition.
- **Failed Exec:** A task with `SIG_DFL` and an active pending interrupt fails `api_exec` (e.g. `ENOENT`).
  - **Expected:** The task retains `SIG_DFL` and the pending bit is unmodified. The failed call returns normally with its documented error. Pending delivery occurs only at an actual safe boundary: before native program entry or after cb_task_yield_as resumes. A failed exec that never yields does not itself introduce a delivery boundary.

### 5. Ignored Pending Discard
- **Action:** An internal interrupt is queued (marking the pending bit) for a task with `SIG_DFL`. Before delivery, the task calls the proposed `cb_api_v1` operation to set `SIG_IGN`.
  - **Expected:** The setter succeeds, returns the old disposition (`SIG_DFL`), and **discards** the pending interrupt by clearing the bit. A subsequent yield will not terminate the task.

### 6. Delivery on Own Stack with Immediate Cleanup Before Reap
- **Action:** An interrupted task awakes and resumes on its own execution stack (`cb_task_yield_as`).
  - **Expected:** Delivery occurs exclusively on the task's stack. It safely closes its owned descriptors and frees memory allocations explicitly *before* waking its parent. `waitpid` accurately reaps the completely cleaned-up task with status `130`.

### 7. Old / Short API and Executor Prefixes
- **Short API:** The runtime uses an older `cb_api_v1` struct lacking the new setter.
  - **Expected:** The caller's attempt to use the private signal veneer safely observes the missing capability, returning `SIG_ERR` and setting `ENOSYS` without mutating state.
- **Short Executor Table:** An older executor is registered without the capability word.
  - **Expected:** Both the kernel request function and the task's disposition setter reject the task with `ENOSYS`, leaving the disposition/pending state untouched and never waking a blocked task.

### 8. Unsupported Signals/Handlers Atomic Errors
- **Action:** A task attempts to set a handler for `SIGKILL`, or uses an arbitrary function address instead of `SIG_DFL`/`SIG_IGN`.
  - **Expected:** The setter instantly returns `SIG_ERR` and sets `EINVAL` (unsupported signal number) or `ENOSYS` (arbitrary handler address). The old disposition and pending state remain absolutely unchanged, exhibiting atomic rejection.

### 9. Pending Interrupts Across Both Exec Phases
- **Action:** A task is blocked in the `EXEC_PENDING` phase. An internal interrupt is queued.
  - **Expected:** The task is **not** immediately transitioned to `RUNNABLE`, preventing a fatal return from `api_exec`. The pending bit is recorded. After `task_finish_exec` commits the new image, delivery occurs inside the new `native_entry`, immediately *before* it invokes the program, terminating the task with `130`.

### 10. Backend Unsupported Behavior
- **Action:** An active task with an ignored disposition (or a pending interrupt) calls `api_exec` specifying an executor target that lacks the cooperative-interrupt-delivery bit.
  - **Expected:** `api_exec` intercepts the capability mismatch and rejects the exec with `ENOSYS` *before* destroying the old image. Both the old image and its signal state survive intact.
- **Action:** An active task with `SIG_DFL` and no pending interrupt execs to the same unsupported executor.
  - **Expected:** The exec succeeds (no signal state to preserve). Any subsequent request against the new task fails explicitly with `ENOSYS`.

## Source-Grounded Uncertainties
- **Header Representation for Dispositions:** The design proposes using `NULL` for `SIG_DFL` and distinct private function identity markers for `SIG_IGN`/`SIG_ERR`. Markers have function-pointer identity and are never invoked. Integer-to-function-pointer casts are forbidden. Ordinary-source tests must establish compatible declarations, comparisons and link identity on each supported compiler. Explicit ordinary-source compile and link tests are strictly required before approving the final header ABI representation.

## Coordinator additions: bounded observations and omitted cases

All rows above and below are **unexecuted acceptance specifications**. No
signal callback, internal request operation or capability bit is implemented by
this note. Use the real proposed request entry point; do not simulate success
by directly setting task fields. Every executor/host spy needs a finite call
budget that fails the fixture, including diagnostic writes.

### Delivery and ownership observations

Give the target one tracked allocation, an open descriptor and an active peer.
At the normal executor termination callback, before parent wait/reap or kernel
teardown, assert the target allocation identity is gone and the descriptor's
owned reference has been released. Confirm the peer still owns its allocation
and descriptor, then allow reaping. Count target termination and cleanup once;
repeated queued requests must not increment either count twice. Compare the
executing context identity to the target's context at delivery: no scheduler or
host callback may perform target termination. Run an after-implementation
control removing immediate cleanup while retaining later reap cleanup; this
fixture must fail at the immediate observer, not silently pass after teardown.

Queue before first native entry: the command's entry counter remains zero,
normal termination runs once and waitpid returns 130. For an existing task,
queue while suspended and deliver after its own yield resumes. A task that
never yields is explicitly not preempted; bound this test by a finite owned
command loop that yields at a chosen step, rather than wall-clock waiting.

Test actual states from src/internal.h: CB_TASK_BLOCKED_PIPE (both empty-read
and full-write), CB_TASK_BLOCKED_CONSOLE, CB_TASK_BLOCKED_WAIT and
CB_TASK_BLOCKED_POLL. There is no separate sleep state; map any sleep-like
operation to its implemented poll/timer behavior. Default requests wake the
supported blocked state; ignored requests leave it blocked. Supply real input,
pipe space, child completion or mock clock advance afterward to prove ignored
operations still complete with their original data/result. Poll fixtures must
show no retained pointer into a terminated stack. An unrelated blocked peer
must remain blocked and retain its own buffers, state and cleanup counts.

### Request rejection and table bounds

Missing, zombie and dead PIDs return the design's negative ENOENT-style project
error; no task is resumed or modified. The implementation must name the actual
project constant, not invent a public kill() errno contract. For short executor
tables, allocate only the original prefix; ordinary registration and execution
still succeed. Setter/request reject interrupts with ENOSYS without reading
past that allocation. A full table with the capability unset or only unknown
bits set behaves identically. Test both supported native execution and an
explicitly opted-in wrapper, never infer capability from delegation.

For the private signal veneer use an actually short API allocation and a full
table with an absent callback: SIG_ERR/ENOSYS, no mutation. On supported success,
return the real previous marker and preserve incoming errno. Invalid signal
number yields EINVAL; an arbitrary handler yields ENOSYS. These are private
signal() return contracts, distinct from the internal setter's integer/output
argument contract. Invalid internal disposition/output arguments must reject
without mutation; do not pass raw function pointers through that ABI.

### Exec phase and inheritance controls

Before task_finish_exec installs the new program, CB_TASK_EXEC_PENDING holds
both current and pending programs. Validate both executors. An unsupported
pending target rejects a request without changing pending state or wakeups.
Never change this state to CB_TASK_RUNNABLE to resume the old successful-exec
continuation. With supported executors, finish exec and deliver on the new
context before its command entry counter increments.

During task_finish_exec, task->program becomes the new program and
pending_program is cleared BEFORE cb_executor_instance_create. A test injection
at that actual allocation boundary must validate the installed target without
dereferencing NULL pending_program, must retain the pending bit, and must not
terminate from the allocator/scheduler stack. Complete context creation then
deliver at the native entry boundary. These are two separate tests, not one
unspecified exec-pending row.

For unsupported target exec, reject pending/ignored state before any irreversible
old-image destruction or partial new-image publication. Assert original context,
argv/environment and resource ownership survive. Default with no pending bit
may exec normally to an unsupported executor, whose later request fails ENOSYS.
For failed ENOENT exec, preserve pending/default state and normal error return;
then explicitly yield and observe delivery. Ignore survives successful exec.
Spawn inherits disposition but not pending; set up the parent pending case with
a bounded supported injection before a non-yielding spawn, and assert parent
and child behavior separately. If the chosen spawn path yields, account for its
real safe delivery boundary rather than assuming the child must be created.

### Review provenance

Coordinator review used the accepted design and actual src/core.c
(task_finish_exec), src/internal.h state enum and src/executor.c entry path.
Corrections preserve Antigravity's original c5c1730 branch in its worktree.
This document does not claim executed tests or accepted SIG-01 implementation.
