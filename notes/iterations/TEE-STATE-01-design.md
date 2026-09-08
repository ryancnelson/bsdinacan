# TEE-STATE-01-design: tee Module State Management

## Goal
Design a state isolation wrapper for the NetBSD `tee` utility to safely manage its mutable global `LIST *head` pointer across independent, concurrent, or sequentially interleaved `cannedBSD` tasks sharing the same host process.

## Background and Prerequisites
The `tee` utility uses a global variable `LIST *head` to track dynamically allocated file descriptors. Upstream `tee.c` never frees these allocations, relying on process exit for reclamation. In `cannedBSD`, tasks multiplex inside a single host process, so the global `head` would leak or cross-contaminate isolated tasks if left unmanaged.

To respect the `cannedBSD` architectural constraint (upstream source files remain byte-for-byte unmodified), we must manage this global state externally, scoping only the `tee`-visible list without over-generalizing to a full TLS system.

## Design

### 1. Compile-Time Symbol Renaming
To capture the global without modifying `tee.c`, we will rename it during compilation via the build system:
`-Dhead=cb_tee_head`

This gives the `cannedBSD` module boundary direct, unambiguous control over the symbol `cb_tee_head`.

### 2. Execution Delegation via `cb_executor_ops` Wrapper
We will define a `cb_tee_program` implementing the `cb_executor_ops` interface. It intercepts lifecycle and context-switch events to swap the global `cb_tee_head` pointer safely, while delegating the actual work to the inner native execution methods.

```c
struct _list; /* Actual forward declaration matching upstream */

struct cb_tee_program {
    struct cb_program common;
    struct cb_program *inner_program;
};

struct cb_tee_execution {
    struct cb_execution common;
    struct cb_execution *inner_execution;
    struct _list *task_head; /* Typed per-execution pointer to hold the list head */
};
```

**Note on Delegation:** The wrapper will delegate each context method using the inner native executor ops provided to it during `prepare()`. It will never use an invented or external `cb_native_executor` API, and it strictly uses the inner execution structure for delegation, never the wrapper layout.

### 3. Context Save and Restore on Yields
When a task is scheduled to run, the wrapper injects its isolated list state. When the native scheduler returns, it handles state extraction and cleanup:

- **`start_or_resume`**: 
  1. Inject the task-owned list state: `cb_tee_head = tee_execution->task_head;`
  2. Delegate to the inner executor: `tee_execution->inner_execution->executor->start_or_resume(tee_execution->inner_execution);`
  3. **Upon Return (Yield):** Check if the task is live. If the task state is `CB_TASK_ZOMBIE` or `CB_TASK_DEAD`, discard the saved state without reading the dangling global.
  4. If the task is live, save the state: `tee_execution->task_head = cb_tee_head;`
  5. Clear the global to `NULL` before returning to the scheduler. Do **not** attempt to restore any potentially dangling prior global state.

This ensures no global reset is ever performed during create/destroy of another task, and the global is always `NULL` while other tasks run.

### 4. Lifecycle Cleanup (Exit/Teardown/Exec)
**CRITICAL RULE:** The `cb_executor_ops` wrapper must **NEVER** traverse or free the `LIST` nodes in executor cleanup. 

The task allocator owns the node allocations (via `cb_allocate`), not the wrapper. Core's `task_release_allocations()` automatically frees them *before* `request_termination` (core.c:938/949) and *before* `instance_destroy` on teardown (core.c:541/544). Attempting to traverse or free them in the wrapper would result in a use-after-free or double-free.

- **`instance_destroy` / `request_termination`**:
  - The wrapper owns **only** its sidecar execution structure (`struct cb_tee_execution`) and the inner context.
  - Simply delegate to the inner executor, then `cb_release` the wrapper structure itself. Do not touch `task_head` or the nodes it points to.
- **`exec` behavior**:
  - A successful `exec` destroys the old execution (including the `tee` wrapper and its heap footprint) before the heap release, and the new fresh instance starts cleanly with a `NULL` state.
  - A failed `exec` preserves the state unharmed.

## Synthetic Verification Plan

To prove this strict isolation contract, we will build synthetic acceptance tests checking:
1. **Repeated Execution:** Run `tee` sequentially; ensure the list size and content start fresh.
2. **Interleaved Execution:** Spawn concurrent `tee` tasks. Yield between them and ensure neither pollutes the global `head` of the other.
3. **Task-Failure / Exec / Teardown:** Force an `err()`, a failed `exec()`, a successful `exec()`, and a standard teardown mid-loop. 
4. **Allocation Observation:** Observe ownership counters *before* teardown (not just via ASan/UBSan) to verify the core `task_release_allocations` correctly reclaims the `add()` list nodes natively, proving zero leaks and safe unwinding without wrapper-level traversal.
