# SIG-01-matrix

## Base Evidence & Scope
- **Base Commit:** `adf62f122a675706f0df7681b6cfb0ee2f039062` (origin/main)
- **Goal:** Derive concrete unexecuted acceptance cases for the cooperative interrupt delivery design documented in `notes/iterations/SIG-01-design.md`.
- **Scope Constraints:** Documentation only. No runtime implementation, no test rig usage, and no rollup edits. Solaris is accepted; signals precede `tee`, not formatting.

## Identified Symbols & Proposed Bounds

- **Existing Symbols:** `api_exec`, `cb_task_yield_as`, `cb_kernel_register`, `task_finish_exec`, `EXEC_PENDING`, `RUNNABLE`, `waitpid` (project convention returns raw exit code).
- **Proposed Additions (No invented names):**
  - An append-only `cb_api_v1` signal setter operation utilizing explicit ABI integer disposition values. The private `signal()` veneer maps between these ABI integers and its private pointer markers (`SIG_IGN`, `SIG_ERR`); private signal pointer markers are not ABI integers, and there are absolutely no integer-to-function-pointer casts.
  - An append-only capability word on `cb_executor_ops` defining a cooperative-interrupt-delivery bit, guarded by an explicit size check against the original prefix.
  - An internal queue/request function taking a kernel and internal PID, acting as the real request boundary.
  - Task-owned scalar fields for disposition and a coalesced pending interrupt bit.

## Acceptance Matrix (Unexecuted Cases)

### 1. Default / Ignore States & Blocked Task Wakeups
- **Action:** A task with `SIG_DFL` is explicitly blocked (tested across `sleep`, `wait`, `read`, `write`, and `poll` states). An internal interrupt is queued.
  - **Expected:** Task state becomes `RUNNABLE`. Delivery occurs precisely at the next `cb_task_yield_as` yield-resume boundary. The task executes its own cleanup and exits with status `130`. `waitpid` observes `130`.
- **Action:** A task with `SIG_IGN` is explicitly blocked (across the same states) and receives a request.
  - **Expected:** Request is quietly discarded. The task remains blocked until valid input or a natural timeout arrives.

### 2. Pending Coalescing
- **Action:** A task with `SIG_DFL` receives multiple identical internal interrupt requests before it next yields.
  - **Expected:** The pending interrupt bit is set once (coalesced). Upon delivery at the next safe boundary, the task cleans up and terminates exactly once. No duplicate destruction paths are invoked.

### 3. Spawn Inheritance Without Pending
- **Action:** A parent task with `SIG_IGN` spawns a child.
  - **Expected:** The child inherits `SIG_IGN`.
- **Action:** A parent task with `SIG_DFL` and an active pending interrupt bit spawns a child.
  - **Expected:** The child inherits `SIG_DFL` but **never** inherits the pending bit. The child runs cleanly; the parent terminates with `130` upon its own next boundary.

### 4. Successful / Failed Exec & Both Exec Phases
- **Exec Phase 1 (`EXEC_PENDING` with pending program):** A task calls `api_exec` and is blocked awaiting `task_finish_exec`. An internal interrupt is queued.
  - **Expected:** The task is **not** immediately transitioned to `RUNNABLE` (preventing a fatal return from `api_exec`). The pending bit is recorded.
- **Exec Phase 2 (`pending_program == NULL`):** After `task_finish_exec` completes the image transition (clearing `pending_program` to `NULL`) but *before* the new program starts.
  - **Expected:** Pending delivery intercepts the execution exactly at the `native_entry` boundary, terminating the task with `130` before the new program ever executes.
- **Failed Exec:** A task calls `api_exec` which fails synchronously (e.g., `ENOENT`). An interrupt is queued.
  - **Expected:** The failed exec retains the `SIG_DFL` disposition and the pending bit is preserved. Delivery does **not** occur during the failure path; it strictly waits for the next explicit `cb_task_yield_as` yield-resume boundary to terminate the task with `130`.

### 5. Ignored Pending Discard
- **Action:** An internal interrupt is queued (marking the pending bit) for a task with `SIG_DFL`. Before delivery, the task calls the proposed `cb_api_v1` operation to set `SIG_IGN`.
  - **Expected:** The setter succeeds, returns the old disposition (`SIG_DFL`), and **discards** the pending interrupt by clearing the bit. A subsequent yield will not terminate the task.

### 6. Delivery on Own Stack & Peer Isolation
- **Action (Before-Entry):** An interrupt is queued for a newly created task *before* its first `native_entry`.
  - **Expected:** The task cleans up and exits with `130` at the `native_entry` boundary before the program ever runs.
- **Action (Own Stack Cleanup & Counters):** An interrupted task awakes and resumes on its own execution stack (`cb_task_yield_as`).
  - **Expected:** Delivery occurs exclusively on the task's stack. It safely closes its owned descriptors and frees memory allocations. This cleanup must be explicitly observed via active descriptor/allocation counters decreasing *before* `waitpid` reaps the task with status `130`.
- **Action (Active Peer Isolation):** A request is queued for Task A while Task B runs.
  - **Expected:** Task B is completely unaffected. Only Task A transitions to `RUNNABLE` and cleans up on its own stack.

### 7. Missing / Dead PIDs and Short Executor Tables
- **Missing / Dead PID:** The internal queue function is called for a non-existent or zombie PID.
  - **Expected:** The request function correctly returns a negative `ENOENT`-style project error without crashing or mutating active tasks.
- **Short Executor Table:** An older executor is registered without the capability word.
  - **Expected:** Both the kernel request function and the task's disposition setter actively reject the task with `ENOSYS`, leaving the disposition/pending state untouched and never waking a blocked task.

### 8. Unsupported Signals/Handlers Atomic Errors
- **Action:** A task attempts to set a handler for `SIGKILL`, or uses an arbitrary function address instead of the mapped private pointer markers (`SIG_DFL`/`SIG_IGN`).
  - **Expected:** The setter instantly returns `SIG_ERR` and sets `EINVAL` (unsupported signal number) or `ENOSYS` (arbitrary handler address). The old disposition and pending state remain absolutely unchanged, exhibiting atomic rejection.

### 9. Backend Unsupported Behavior
- **Action:** An active task with an ignored disposition (or a pending interrupt) calls `api_exec` specifying an executor target that lacks the cooperative-interrupt-delivery bit.
  - **Expected:** `api_exec` intercepts the capability mismatch and rejects the exec with `ENOSYS` *before* destroying the old image. Both the old image and its signal state survive intact.
- **Action:** An active task with `SIG_DFL` and no pending interrupt execs to the same unsupported executor.
  - **Expected:** The exec succeeds (no signal state to preserve). Any subsequent request against the new task fails explicitly with `ENOSYS`.
