# Iteration Design: IO-01 Public Descriptor Polling

## Concrete poll ABI and ordinary libc types

To provide public descriptor polling matching POSIX expectations, we introduce the following types and constants into `include/cannedbsd/abi.h`:

```c
struct cb_pollfd {
    int fd;
    short events;
    short revents;
};

#define CB_POLLIN   0x001
#define CB_POLLPRI  0x002
#define CB_POLLOUT  0x004
#define CB_POLLERR  0x008
#define CB_POLLHUP  0x010
#define CB_POLLNVAL 0x020
```

The public API entry point will be added to the end of `struct cb_api_v1`:

```c
    int (*poll)(struct cb_pollfd *fds, size_t nfds, int timeout);
```

## Old-size compatibility

The `struct cb_api_v1` is provided by the kernel to the running program. Because the ABI relies on `struct_size` for evolution:

- **Kernel Side:** The kernel populates `cb_api_v1` fully. Older programs compiled against smaller struct definitions will simply ignore the new `poll` field.
- **Libc Side:** The libc wrapper for `poll()` must verify the host kernel supports it. It will check if `api->struct_size >= offsetof(struct cb_api_v1, poll) + sizeof(void *)`. If the kernel's provided struct is too small, libc will return `-1` and set `errno` to `ENOSYS`.

## Task ownership

The `struct cb_pollfd` array is provided and owned by the calling task's memory space. 

Because `api_poll` operates synchronously from the perspective of the task (the task execution is fully suspended while blocked inside the kernel call), the kernel can iterate and write directly to the caller's array in-place. No intermediate kernel allocations or copies of the `cb_pollfd` array are needed. 

## Timeout semantics given optional clocks

The `timeout` parameter specifies the maximum time to wait in milliseconds.
- `timeout < 0`: Block indefinitely.
- `timeout == 0`: Return immediately without blocking (non-blocking).
- `timeout > 0`: Block for up to the specified milliseconds.

**Open Design Decision:** What happens if `timeout > 0`, but the host environment cannot provide a monotonic clock (`monotonic_millis` returns `0`)?
- **Proposed Choice:** Degrade a positive timeout to an infinite timeout (`timeout = -1`).
- **Justification:** If we cannot measure elapsed time, yielding iteratively risks becoming a busy-wait loop that hogs the CPU, while returning immediately breaks the blocking intent of the caller. A missing host clock in an emulator is an edge case, and safely waiting for actual I/O readiness correctly fulfills the primary goal of `poll`.

## Wakeup registration/cancellation

CannedBSD utilizes a centralized, sweep-based scheduling model rather than per-object wait queues or callback registrations.

1. When `api_poll` finds no ready descriptors, it calculates a `wake_deadline` (if timeout > 0) and yields the task using a new state: `CB_TASK_BLOCKED_POLL`.
2. Existing readiness notifications like `cb_wake_pipe_tasks()` and `wake_console_waiters()` will be updated to unconditionally transition `CB_TASK_BLOCKED_POLL` tasks to `CB_TASK_RUNNABLE` (in addition to their specific blocked states).
3. The scheduler's `pick_runnable()` function will be updated to check `wake_deadline` against `monotonic_millis()`. If the deadline has passed, it awakens the task.
4. Once awoken, `api_poll` simply re-evaluates `file->ops->poll()` for the array. If events are ready or the timeout expired, it populates `revents` and returns. Otherwise, it yields again.

This design completely avoids maintaining host descriptor registrations (like epoll/kqueue) and keeps the internal state strictly deterministic.

## Errors and readiness mapping

- `poll` returns the number of ready descriptors (with non-zero `revents`), `0` on timeout, or negative `CB_E*` codes on structural errors (e.g., `-CB_EINVAL` for bad `nfds`).
- `fd < 0`: Ignored, and `revents` is set to `0`.
- **Invalid FD:** Descriptors exceeding the table bounds or pointing to closed files return `CB_POLLNVAL` in `revents`.
- **Regular Files:** Always return `CB_POLLIN | CB_POLLOUT`.
- **Pipes:** 
  - Readers return `CB_POLLIN` if data is available or writers are closed (`CB_POLLHUP`).
  - Writers return `CB_POLLOUT` if space is available. They return `CB_POLLERR` if readers are closed.
- **Console:** Maps to `CB_POLLIN` / `CB_POLLOUT` according to the host polling callbacks.

## Minimal deterministic red/green implementation sequence

1. **Red Test:** Add a `test_poll` in `tests/test_core.c` that validates multiple descriptor polling, positive timeouts, and invalid FDs. The test will fail because `poll` does not exist in `api`.
2. **ABI Definition:** Add `struct cb_pollfd` and constants to `include/cannedbsd/abi.h`. Append `poll` to `struct cb_api_v1`.
3. **Architecture Check:** Update `tests/test_architecture.sh` to expect the increased `struct cb_api_v1` size.
4. **Task State & Clock:** Introduce `CB_TASK_BLOCKED_POLL` to `enum cb_task_state` and a `uint64_t wake_deadline` field to `struct cb_task`.
5. **Scheduler Wakeups:** Modify `pick_runnable` to enforce timeouts, and update `cb_wake_pipe_tasks` and `wake_console_waiters` to awaken polling tasks.
6. **Kernel Implementation:** Implement `api_poll` in `src/core.c` iterating over descriptors and polling their underlying files via the existing `file->ops->poll` seam.
7. **Libc Wrapper:** Expose `poll()` in `libcannedbsd.a` with old-size compatibility checks.
8. **Green Verification:** Run `make ci` and assert that `test_poll` passes reliably under ASan/UBSan without busy loops.
