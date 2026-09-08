# TEE-STATE-01-design: tee Module State Management

## Base
**Base SHA:** 3e02a2cb78edd9732b50ef3c123ab86d7ddda0bd

## Goal
Design a state isolation wrapper for the NetBSD `tee` utility to safely manage its mutable global `LIST *head` pointer across independent, concurrent, or sequentially interleaved `cannedBSD` tasks sharing the same host process.

## Background and Prerequisites
The `tee` utility uses a global variable `LIST *head` to track dynamically allocated file descriptors. Upstream `tee.c` never frees these allocations, relying on process exit for reclamation. In `cannedBSD`, tasks multiplex inside a single host process, so the global `head` would leak or cross-contaminate isolated tasks if left unmanaged.

To respect the `cannedBSD` architectural constraint (upstream source files remain byte-for-byte unmodified), we must manage this global state externally, scoping only the `tee`-visible list without over-generalizing to a full TLS system.

## Design

### 1. Compile-Time Symbol Renaming
To capture the global without modifying `tee.c`, and to avoid collisions with other utilities, we will rename its core symbols during compilation via the build system:
`-Dhead=cb_tee_head -Dmain=cb_tee_main -Dadd=cb_tee_add`

This gives the `cannedBSD` module boundary direct, unambiguous control over these symbols.

### 2. Execution Delegation via `cb_executor_ops` Wrapper
We will define a custom `cb_executor_ops` interface that intercepts lifecycle and context-switch events to swap the global `cb_tee_head` pointer safely.

**Simplification**: We will NOT wrap `cb_program`. Our `prepare()` will simply call `cb_native_executor()->prepare(kernel, custom_ops, source, program_out)`. This naturally embeds our custom ops in the native program layout without needing casts or a `cb_tee_program` structure.

```c
struct _list; /* Actual forward declaration matching upstream */

struct cb_tee_execution {
    struct cb_execution common;
    struct cb_execution *inner_execution;
    struct _list *task_head; /* Typed per-execution pointer to hold the list head */
};
```

**CRITICAL NOTE ON DISPATCH**: The wrapper must explicitly call `cb_native_executor()->method(wrapper->inner_execution)` for delegation. It must **never** dispatch via `wrapper->inner_execution->executor->method(...)` because `native_prepare` records the passed custom ops, and dispatching through it would infinitely recurse back into the wrapper!

### 3. Context Save, Restore, and Creation
When a task is scheduled to run, the wrapper injects its isolated list state.

- **`instance_create`**:
  1. Allocate runtime-owned sidecar (`struct cb_tee_execution`) via `cb_allocate`. If it fails, return `NULL`.
  2. Delegate inner creation: `wrapper->inner_execution = cb_native_executor()->instance_create(task, native_program)`.
  3. If native creation fails, exact unwind: release the sidecar via `cb_release` and return `NULL`.
  4. Initialize the outer `common` execution fields (`executor = custom_ops`, `task`, `program`).
  5. Initialize `wrapper->task_head = NULL`.
  6. Return the wrapper.
  *Note*: No creation or failure path may touch or reset the global `cb_tee_head`, ensuring no active task global is ever disturbed.

- **`start_or_resume`**:
  1. Inject the task-owned list state: `cb_tee_head = wrapper->task_head;`
  2. Explicit delegate: `cb_native_executor()->start_or_resume(wrapper->inner_execution);`
  3. **Upon Return (Yield):** Check if the task is live. If the task state is `CB_TASK_ZOMBIE` or `CB_TASK_DEAD`, discard the saved state without reading the dangling global.
  4. If the task is live, save the state: `wrapper->task_head = cb_tee_head;`
  5. Clear the global to `NULL` before returning to the scheduler. Do **not** attempt to restore any potentially dangling prior global state.

- **`suspend`**:
  - Explicit mandatory delegate: `cb_native_executor()->suspend(wrapper->inner_execution)`.

### 4. Lifecycle Cleanup (Exit/Teardown/Exec)
**CRITICAL RULE 1: NEVER traverse or free `LIST` nodes in executor cleanup.**
The `LIST` nodes use ordinary `malloc` which maps to the task API allocate, tracking payload/bookkeeping for task cleanup. They are **not** direct `cb_allocate` allocations. Core's `task_release_allocations()` automatically frees them *before* `request_termination` (core.c:938/949) and *before* `instance_destroy` on teardown (core.c:541/544). Attempting to traverse or free them in the wrapper would result in a use-after-free or double-free.

**CRITICAL RULE 2: NEVER release the wrapper in `request_termination`.**
Native termination suspends and *never returns*. The release belongs ONLY in `instance_destroy`.

- **`request_termination`**:
  - Explicit delegate: `cb_native_executor()->request_termination(wrapper->inner_execution);`
  - Do NOT release the wrapper sidecar here.
- **`instance_destroy`**:
  - Explicit delegate: `cb_native_executor()->instance_destroy(wrapper->inner_execution);`
  - The sidecar is runtime-owned `cb_allocate` and must be explicitly released here via `cb_release(kernel, wrapper)`.
- **`program_destroy`**:
  - Explicit delegate: `cb_native_executor()->program_destroy(kernel, native_program)`. No extra program-count decrement is performed.
- **`exec` behavior**:
  - A successful `exec` destroys the old execution (which releases the sidecar via `instance_destroy`) before the heap release, and the new fresh instance starts cleanly with a `NULL` state.
  - A failed `exec` preserves the state unharmed.

## Synthetic Verification Plan

To prove this strict isolation contract, we will build synthetic acceptance tests that work **without importing `tee` yet** (e.g., using a dummy program that mimics `tee`'s list allocation and global state usage):
1. **Repeated Execution:** Run the mock sequentially; ensure the list size and content start fresh.
2. **Interleaved Execution:** Spawn concurrent mock tasks. Yield between them and ensure neither pollutes the global `head` of the other.
3. **Creation Failure Unwinding:** Inject each creation failure (sidecar allocation, native context creation). Verify exact unwind paths correctly free runtime-owned sidecars, observe cleanup before teardown, and preserve another active task's global unharmed.
4. **Task-Failure / Exec / Teardown:** Force an `err()`, a failed `exec()`, a successful `exec()`, and a standard teardown mid-loop in the mock load.
5. **Ownership and Allocation Observation:** Observe ownership counters *before* teardown, distinguishing task API list payload/bookkeeping vs runtime-owned sidecar/context ownership. Verify the core `task_release_allocations` correctly reclaims the `malloc` list nodes natively, proving zero leaks without wrapper-level traversal.
