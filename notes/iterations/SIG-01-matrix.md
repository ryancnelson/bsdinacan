# SIG-01-matrix

## Base Evidence & Scope
- **Base Commit:** `adf62f1` (origin/main with Solaris port merged)
- **Goal:** Derive concrete unexecuted acceptance cases for the cooperative interrupt delivery design documented in `notes/iterations/SIG-01-design.md`.
- **Scope:** Documentation only. No runtime implementation, no test rig usage, and no rollup edits. Target priority remains deferred behind Solaris integration and utility formatting tasks.

## Identified Symbols & Proposed Bounds

- **Existing Symbols:** `api_exec`, `cb_task_yield_as`, `cb_kernel_register`, `task_finish_exec`, `EXEC_PENDING`, `RUNNABLE`, `waitpid` (project convention returns raw exit code).
- **Proposed Additions (No invented names):**
  - An append-only `cb_api_v1` signal setter operation utilizing explicit integer disposition values (`SIG_DFL`, `SIG_IGN`), avoiding cast function pointers.
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
  - **Expected:** The task retains `SIG_DFL` and the pending bit is unmodified. The failure path completes, and the pending interrupt is delivered normally before returning to the old image.

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
  - **Expected:** The task is **not** immediately transitioned to `RUNNABLE`, preventing a fatal return from `api_exec`. The pending bit is recorded. After `task_finish_exec` commits the new image, delivery occurs immediately *before* the new `native_entry` begins, terminating the task with `130`.

### 10. Backend Unsupported Behavior
- **Action:** An active task with an ignored disposition (or a pending interrupt) calls `api_exec` specifying an executor target that lacks the cooperative-interrupt-delivery bit.
  - **Expected:** `api_exec` intercepts the capability mismatch and rejects the exec with `ENOSYS` *before* destroying the old image. Both the old image and its signal state survive intact.
- **Action:** An active task with `SIG_DFL` and no pending interrupt execs to the same unsupported executor.
  - **Expected:** The exec succeeds (no signal state to preserve). Any subsequent request against the new task fails explicitly with `ENOSYS`.

## Source-Grounded Uncertainties
- **Header Representation for Dispositions:** The design proposes using `NULL` for `SIG_DFL` and distinct private function identity markers for `SIG_IGN`/`SIG_ERR`. The portability of safely casting/comparing these integers to markers across different host environments (e.g. Linux ELF vs. Mac Retro68) remains unverified. Explicit ordinary-source compile and link tests are strictly required before approving the final header ABI representation.
