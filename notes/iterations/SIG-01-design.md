# SIG-01-design: an honest, bounded interrupt prerequisite

Draft design at accepted `e290168f0e080bd55dbbdcff65f87c4ecd2d2479`.
Everything called proposed below is new work, not an existing API or an
implemented signal system. No runtime changes or successful signal tests are
claimed by this note. Review is required before implementation assignment.

## Why tee needs more than a header

The pinned tee source calls signal(SIGINT, SIG_IGN) for -i and ignores its
return value. Returning ENOSYS cannot make that option work. The current core
has no task signal state or delivery API. Its existing api_exit cleans task
allocations/descriptors and yields through executor termination from the task's
own execution context. Calling that operation from a scheduler/host context
would target the wrong execution stack.

NetBSD documents default, ignored and caught dispositions; selecting ignore
also discards pending instances. Ignored dispositions survive exec, while caught
handlers reset. These are reference semantics, not a claim that this proposal
implements all of them. Sources: [sigaction(2)](https://man.netbsd.org/sigaction.2)
and [execve(2)](https://man.netbsd.org/execve.2).

## Proposed first boundary

Support only SIGINT with default or ignore disposition. Default delivery ends
the internal task with status 130 through its normal cleanup path. This status
is an explicit project convention because current waitpid returns an exit code,
not a POSIX encoded signal wait status. Do not advertise WIFSIGNALED or signals
sent to host processes. Other signal numbers fail EINVAL; arbitrary handler
functions fail ENOSYS and leave disposition/pending state untouched.

Add two task-owned scalar fields: disposition and a coalesced pending interrupt
bit. New root tasks start default with no pending interrupt. Spawn inherits the
parent's disposition but never its pending bit. Successful exec preserves the
pending bit and ignored disposition; default remains default. Failed exec leaves
both unchanged. No mask, handler stack, restart flag or asynchronous host handler
is introduced in this slice.

A proposed append-only cb_api_v1 operation sets a supported disposition and
returns the previous value through an output argument. Use integer disposition
values across that ABI, not raw handler addresses. The private signal() veneer
maps SIG_DFL/SIG_IGN to those values and maps the previous value back. A possible
portable representation uses NULL for SIG_DFL and distinct private function
identity markers for SIG_IGN/SIG_ERR; markers are compared, never invoked. This
representation requires explicit ordinary-source compile/link tests on Linux
and Retro68 before approval of the header. Do not silently cast integers to
function pointers or promise arbitrary callback support.

Check the real allocated API prefix and optional callback before reading it;
missing support yields SIG_ERR/ENOSYS without changing state. Bad numbers,
unsupported handlers and invalid core disposition/output arguments do not
mutate anything. Success preserves incoming errno and returns the actual old
disposition. Selecting ignore clears pending interrupts; subsequent requests
while ignored are discarded without waking a blocked task.

## Proposed request and delivery path

Provide a runtime/internal kernel request function taking a kernel and internal
PID. It queues SIGINT only; it is not a public kill() interface. It returns a
negative ENOENT-style project error for missing, zombie or dead tasks. Requests
must run on the same serialized host thread as the kernel; no async-signal-safe
or cross-thread guarantee is made. Tests call this real entry point rather than
setting task fields directly. A future frontend may wire it to keyboard input;
that host integration is not part of this task and is not claimed by tee tests.

Queueing default disposition marks the pending bit. Wake only blocked pipe,
console, wait and poll states to runnable. Do not turn EXEC_PENDING into
RUNNABLE: api_exec deliberately never returns, and resuming its old continuation
would reach the fatal "successful exec returned" path. Let task_finish_exec
complete normally and deliver before the fresh native entry starts.

Delivery occurs on the target's own stack at explicit safe boundaries: before
native_entry invokes the program, and immediately after cb_task_yield_as resumes
its suspended executor. At that point, pending default delivery clears the bit
and invokes the normal task exit operation. Do not terminate from within an
allocation, descriptor transaction, host callback or scheduler stack. A task
running without yielding cannot be preempted; this is a stated cooperative
limitation. Support must be an explicit executor opt-in, not inferred from structure casts
or delegation. Proposed: append a capabilities word to the versioned internal
cb_executor_ops table, with one cooperative-interrupt-delivery bit. Guard its
actual struct_size before reading the word; a short table or unset bit is
unsupported. Native opts in only when both delivery boundaries are implemented;
a native wrapper must explicitly opt in after tests prove it retains them.
Unknown bits do not establish this particular capability.

The disposition setter and kernel request reject unsupported target executors
with ENOSYS, without changing disposition/pending state or waking a task. For
EXEC_PENDING, request validation must cover BOTH current and pending program
executors; validating only the old image is insufficient. Before api_exec commits
a new image with unsupported delivery, reject ENOSYS if the task has a pending
interrupt or ignored disposition, preserving its old image and signal state.
With default disposition and no pending bit, ordinary exec to an unsupported
executor remains allowed; later requests to it fail ENOSYS. This is an explicit
capability restriction, not unrestricted POSIX exec behavior. Test the check
before every allocation/state-publication boundary that could otherwise leave
partial exec state.

Blocked delivery must close owned descriptors and allocations once, wake peers
through existing close/exit logic, and wake the waiting parent. Review actual
poll wait pointers and pending-exec ownership before implementation: no saved
stack pointer or pending image resource may survive termination incorrectly.
Ordinary early exit before a pending delivery simply follows existing exit;
there is no attempt to resume a dead task to report a signal.

## Required falsifiable work split

1. Core state/request/lifecycle tests, without exposing signal.h yet. A default
   task blocked on a pipe must terminate after a real queued request, release
   its list/descriptor ownership before teardown, and let its waiting parent
   observe 130. An ignored peer must remain blocked until actual input arrives,
   then resume with unchanged data/state. Repeated requests must not duplicate
   cleanup. Include before-first-entry, yield, all blocked states, exec-pending,
   missing/dead PID, spawn inheritance, failed/successful exec and active peer
   isolation. Include actual short executor tables, unset capability, supported
   and unsupported cross-executor exec, and a request during EXEC_PENDING with
   an unsupported pending target; rejection must preserve all state and wakeups. Every mock has a finite call/step budget.
2. Private signal() mapping and actual short-ABI allocations. Test previous
   dispositions, ignored pending discard, preserved errno and rejection without
   mutation. Never compare host signal numbers or call host signal/kill.
3. Only after both pass review, exact three-workflow CI and shared Mac execution,
   use unchanged tee with and without -i as the final integration proof. The
   test must queue a real internal interrupt and show different outcomes;
   compilation or storing a flag alone is insufficient. Header representation,
   core lifecycle boundaries and executor coverage remain review gates.

This design does not authorize general signals, handler delivery, host keyboard
wiring, preemption, umask, permissions, process groups or an unreviewed tee import.

## Priority and transition

The subsequently published Solaris policy at `a1c85ba` takes priority for new
implementation. This existing assigned task completes only the design review;
no signal implementation is assigned before the Solaris integration priority
is handled. Future runtime work must carry the required Solaris gate alongside
Linux and System 7 according to `notes/CI.md`.
