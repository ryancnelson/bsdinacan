#include "internal.h"
#include <string.h>

/* Host/root only, one temporary kernel at a time. No host signal machinery. */
enum {
    PIPE_DEFAULT, PIPE_IGNORE, WRITE_DEFAULT, WRITE_IGNORE,
    CONSOLE_DEFAULT, CONSOLE_IGNORE, WAIT_DEFAULT, WAIT_IGNORE,
    POLL_DEFAULT, POLL_IGNORE, BEFORE_ENTRY, YIELD_DEFAULT, DISCARD,
    IGNORE_EXEC, EXEC_PENDING, EXEC_INSTALLED, SHORT_EXECUTOR,
    UNSET_EXECUTOR, UNKNOWN_EXECUTOR, EXEC_REJECT_PENDING, EXEC_REJECT_IGNORE,
    EXEC_UNSUPPORTED, REQUEST_UNSUPPORTED_PENDING, LATE_EXEC_REQUEST,
    FAILED_EXEC, EARLY_EXIT, SPAWN_PENDING, SPAWN_IGNORE, LIVE_DESTROY,
    CASE_COUNT
};
struct execution {
    struct cb_execution common;
    struct cb_execution *inner;
    struct cb_host_context *context;
};
static const struct cb_host_ops_v1 *base;
static struct cb_kernel *kernel;
static struct cb_task *parent_task, *target_task, *peer_task;
static struct cb_host_context *root, *current_context, *created_context;
static unsigned scenario, error, budget, context_count, root_calls;
static unsigned entered, next_entered, terminated, payloads_freed, injected;
static unsigned allocations_seen, fail_allocation, allocation_armed;
static int pipe_fds[2], peer_fds[2], allow_leaf, input_ready, target_ready;
static uint64_t now;
static void *parent_payload, *target_payload, *peer_payload;
static struct cb_open_file *parent_file;
static struct cb_executor_ops supported, unsupported;

static void tick(void)
{
    if (++budget > 8000) base->fatal("signal fixture exceeded finite callback budget");
}
static void *allocate(size_t size)
{
    tick();
    if (allocation_armed) {
        ++allocations_seen;
        if (fail_allocation && allocations_seen == fail_allocation) return NULL;
        if (scenario == LATE_EXEC_REQUEST && !injected) {
            ++injected;
            if (cb_kernel_request_interrupt(kernel, target_task->pid) != 0)
                error = 70;
        }
    }
    if (scenario == EXEC_INSTALLED && target_task != NULL && !injected &&
        target_task->state == CB_TASK_EXEC_PENDING && target_task->pending_program == NULL) {
        ++injected;
        if (current_context != root || target_task->execution != NULL ||
            strcmp(target_task->program->name, "next") != 0 ||
            cb_kernel_request_interrupt(kernel, target_task->pid) != 0 ||
            target_task->state != CB_TASK_EXEC_PENDING) error = 71;
    }
    return base->allocate(size);
}
static void release(void *p)
{
    tick();
    if (p != NULL && p == target_payload) { target_payload = NULL; ++payloads_freed; }
    if (p != NULL && p == peer_payload) peer_payload = NULL;
    if (p != NULL && p == parent_payload) parent_payload = NULL;
    base->release(p);
}
static struct cb_host_context *context_root(void)
{
    tick(); ++root_calls;
    root = base->context_root(); current_context = root;
    if (root != NULL) ++context_count;
    return root;
}
static struct cb_host_context *context_create(void (*entry)(void *), void *arg, size_t n)
{
    tick(); created_context = base->context_create(entry, arg, n);
    if (created_context != NULL) ++context_count;
    return created_context;
}
static void context_switch(struct cb_host_context *from, struct cb_host_context *to)
{
    tick(); current_context = to;
    base->context_switch(from, to);
    current_context = from;
}
static void context_destroy(struct cb_host_context *p)
{
    tick();
    if (p != NULL) {
        if (context_count == 0) error = 72;
        else --context_count;
    }
    base->context_destroy(p);
}
static int console_poll(int timeout)
{ (void)timeout; tick(); return input_ready; }
static cb_ssize_t console_read(void *buffer, size_t count)
{
    tick();
    if (!input_ready || count == 0) return -CB_EIO;
    *(char *)buffer = 'Q'; input_ready = 0; return 1;
}
static cb_ssize_t console_write(int stream, const void *buffer, size_t count)
{ (void)stream; (void)buffer; tick(); return (cb_ssize_t)count; }
static uint64_t clock_now(void) { tick(); return now; }
static void host_yield(void) { tick(); }
static int prepare(struct cb_kernel *k, const struct cb_executor_ops *ops,
                   const void *source, struct cb_program **out)
{ tick(); return cb_native_executor()->prepare(k, ops, source, out); }
static struct cb_execution *create(struct cb_task *task, const struct cb_program *program)
{
    struct execution *e;
    tick();
    e = cb_allocate(task->kernel, sizeof(*e));
    if (e == NULL) return NULL;
    e->inner = cb_native_executor()->instance_create(task, program);
    if (e->inner == NULL) { cb_release(task->kernel, e); return NULL; }
    e->context = created_context;
    e->common.executor = program->executor; e->common.task = task;
    e->common.program = program;
    return &e->common;
}
static void resume(struct cb_execution *common)
{
    struct execution *e = (struct execution *)common;
    tick();
    cb_native_executor()->start_or_resume(e->inner);
    if ((scenario == EXEC_PENDING || scenario == REQUEST_UNSUPPORTED_PENDING) &&
        common->task->state == CB_TASK_EXEC_PENDING && !injected) {
        int expected = scenario == EXEC_PENDING ? 0 : -CB_ENOSYS;
        ++injected;
        if (common->task->pending_program == NULL || current_context != root ||
            cb_kernel_request_interrupt(kernel, common->task->pid) != expected ||
            common->task->state != CB_TASK_EXEC_PENDING ||
            common->task->interrupt_pending != (expected == 0)) error = 73;
    }
}
static void suspend(struct cb_execution *common)
{ tick(); cb_native_executor()->suspend(((struct execution *)common)->inner); }
static void terminate(struct cb_execution *common)
{
    struct execution *e = (struct execution *)common;
    unsigned i, references = 0;
    struct cb_task *t;
    tick(); ++terminated;
    if (common->task->state != CB_TASK_ZOMBIE ||
        cb_kernel_request_interrupt(kernel, common->task->pid) != -CB_ENOENT ||
        common->task->state != CB_TASK_ZOMBIE) error = 77;
    /* Observe before wait/reap/context destruction, including exact heap identity. */
    if (current_context != e->context || target_payload != NULL ||
        common->task->allocations != NULL || parent_payload == NULL ||
        parent_task->allocations == NULL) error = 74;
    if (peer_task != NULL && (peer_payload == NULL || peer_task->allocations == NULL ||
        peer_task->state != CB_TASK_BLOCKED_PIPE)) error = 78;
    for (i = 0; i < CB_MAX_FDS; ++i)
        if (common->task->descriptors[i].file != NULL) error = 75;
    for (t = kernel->tasks; t != NULL; t = t->next)
        for (i = 0; i < CB_MAX_FDS; ++i)
            if (t->descriptors[i].file == parent_file) ++references;
    if (references == 0 || parent_file->references != references) error = 76;
    cb_native_executor()->request_termination(e->inner);
}
static void destroy(struct cb_execution *common)
{
    struct execution *e = (struct execution *)common;
    tick(); cb_native_executor()->instance_destroy(e->inner);
    cb_release(common->task->kernel, e);
}
static void program_destroy(struct cb_kernel *k, struct cb_program *program)
{ tick(); cb_native_executor()->program_destroy(k, program); }

static int peer(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    char byte;
    (void)argc; (void)argv; (void)envp;
    peer_payload = api->allocate(29);
    if (peer_payload == NULL) return 65;
    return api->read(peer_fds[0], &byte, 1) == 1 && byte == 'P' ? 0 : 66;
}
static int leaf(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    unsigned i;
    (void)argc; (void)argv; (void)envp;
    for (i = 0; !allow_leaf && i < 50; ++i) api->yield();
    return allow_leaf ? 0 : 60;
}
static int next(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    int old = -1;
    (void)argc; (void)argv; (void)envp;
    ++next_entered;
    if (scenario == IGNORE_EXEC) {
        if (target_task->interrupt_disposition != CB_INTERRUPT_IGNORE ||
            cb_kernel_request_interrupt(kernel, target_task->pid) != 0 ||
            target_task->interrupt_pending ||
            cb_task_set_interrupt(target_task, CB_INTERRUPT_DEFAULT, &old) != 0 ||
            old != CB_INTERRUPT_IGNORE) return 61;
        api->yield();
        return 0;
    }
    if (scenario == EXEC_UNSUPPORTED || scenario == REQUEST_UNSUPPORTED_PENDING) {
        if (cb_kernel_request_interrupt(kernel, target_task->pid) != -CB_ENOSYS ||
            cb_task_set_interrupt(target_task, CB_INTERRUPT_IGNORE, &old) != -CB_ENOSYS ||
            old != -1 || target_task->interrupt_pending) return 62;
        return 0;
    }
    return 63; /* Pending default delivery must prevent this entry. */
}
static int inherited(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct cb_task *self = kernel->current;
    (void)argc; (void)argv; (void)envp;
    ++next_entered;
    if (self->interrupt_pending || self->interrupt_disposition !=
        (scenario == SPAWN_IGNORE ? CB_INTERRUPT_IGNORE : CB_INTERRUPT_DEFAULT)) return 64;
    api->yield(); return 0;
}
static int target(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    char byte;
    char *args[] = {"next", NULL};
    int old, status;
    cb_pid_t pid;
    unsigned i;
    struct cb_task *self = kernel->current;
    (void)argc; (void)argv;
    ++entered; target_task = self;
    target_payload = api->allocate(19);
    if (target_payload == NULL) return 20;
    if (scenario <= POLL_IGNORE) {
        if ((scenario & 1) && cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, &old) != 0)
            return 21;
        target_ready = 1;
        switch (scenario / 2) {
        case 0: return api->read(pipe_fds[0], &byte, 1) == 1 && byte == 'Q' ? 0 : 22;
        case 1: return api->write(pipe_fds[1], "Q", 1) == 1 ? 0 : 23;
        case 2: return api->read(0, &byte, 1) == 1 && byte == 'Q' ? 0 : 24;
        case 3:
            args[0] = "leaf";
            if (api->spawn(args[0], args, envp, NULL, 0, &pid) != 0) return 25;
            return api->waitpid(pid, &status) == pid && status == 0 ? 0 : 26;
        case 4: return api->poll(NULL, 0, 20) == 0 ? 0 : 27;
        }
    }
    if (scenario == YIELD_DEFAULT || scenario == LIVE_DESTROY) {
        target_ready = 1; api->yield(); return 0;
    }
    if (scenario == DISCARD) {
        api->set_errno(CB_EPIPE); old = 99;
        if (cb_kernel_request_interrupt(kernel, self->pid) != 0 ||
            cb_kernel_request_interrupt(kernel, self->pid) != 0 ||
            !self->interrupt_pending || self->state != CB_TASK_RUNNING ||
            cb_task_set_interrupt(self, 77, &old) != -CB_EINVAL || old != 99 ||
            cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, NULL) != -CB_EINVAL ||
            !self->interrupt_pending || self->interrupt_disposition != CB_INTERRUPT_DEFAULT ||
            cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, &old) != 0 ||
            old != CB_INTERRUPT_DEFAULT || self->interrupt_pending || api->get_errno() != CB_EPIPE)
            return 28;
        api->yield();
        if (cb_kernel_request_interrupt(kernel, self->pid) != 0 || self->interrupt_pending ||
            cb_task_set_interrupt(self, CB_INTERRUPT_DEFAULT, &old) != 0 ||
            old != CB_INTERRUPT_IGNORE || api->get_errno() != CB_EPIPE) return 29;
        return 0;
    }
    if (scenario == SHORT_EXECUTOR || scenario == UNSET_EXECUTOR || scenario == UNKNOWN_EXECUTOR) {
        old = 99; api->set_errno(CB_EPIPE);
        if (cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, &old) != -CB_ENOSYS ||
            cb_kernel_request_interrupt(kernel, self->pid) != -CB_ENOSYS ||
            old != 99 || self->interrupt_pending || self->interrupt_disposition != CB_INTERRUPT_DEFAULT ||
            api->get_errno() != CB_EPIPE) return 30;
        target_ready = 1;
        return api->read(pipe_fds[0], &byte, 1) == 1 && byte == 'Q' ? 0 : 31;
    }
    if (scenario == SPAWN_PENDING || scenario == SPAWN_IGNORE) {
        if (scenario == SPAWN_PENDING) {
            if (cb_kernel_request_interrupt(kernel, self->pid) != 0) return 32;
        } else if (cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, &old) != 0) return 33;
        args[0] = "inherited";
        if (api->spawn(args[0], args, envp, NULL, 0, &pid) != 0) return 34;
        target_ready = 1;
        if (api->waitpid(pid, &status) != pid || status != 0) return 35;
        return 0;
    }
    if (scenario == EARLY_EXIT) {
        if (cb_kernel_request_interrupt(kernel, self->pid) != 0) return 36;
        return 7;
    }
    if (scenario == FAILED_EXEC || scenario == EXEC_REJECT_PENDING) {
        if (cb_kernel_request_interrupt(kernel, self->pid) != 0) return 37;
    }
    if (scenario == IGNORE_EXEC || scenario == EXEC_REJECT_IGNORE) {
        if (cb_task_set_interrupt(self, CB_INTERRUPT_IGNORE, &old) != 0) return 38;
    }
    if (scenario == FAILED_EXEC) {
        const struct cb_execution *execution = self->execution;
        char **old_argv = self->argv;
        if (api->exec("absent", args, envp) != -1 || api->get_errno() != CB_ENOENT ||
            !self->interrupt_pending || self->execution != execution || self->argv != old_argv)
            return 39;
        /* Copy failures must also leave the old image/pending state intact. */
        for (i = 1; i <= 4; ++i) {
            allocations_seen = 0; fail_allocation = i; allocation_armed = 1;
            status = api->exec("next", args, envp); allocation_armed = 0;
            if (status != -1 || api->get_errno() != CB_ENOMEM || !self->interrupt_pending ||
                self->execution != execution || self->argv != old_argv || self->pending_program != NULL)
                return 40;
        }
        fail_allocation = 0; api->yield(); return 41;
    }
    if (scenario == EXEC_REJECT_PENDING || scenario == EXEC_REJECT_IGNORE || scenario == LATE_EXEC_REQUEST) {
        const struct cb_execution *execution = self->execution;
        char **old_argv = self->argv, **old_env = self->environment;
        unsigned frees = payloads_freed;
        allocations_seen = 0; allocation_armed = 1;
        status = api->exec("next", args, envp); allocation_armed = 0;
        if (status != -1 || api->get_errno() != CB_ENOSYS || self->execution != execution ||
            self->argv != old_argv || self->environment != old_env || target_payload == NULL ||
            payloads_freed != frees || self->pending_program != NULL ||
            (scenario != LATE_EXEC_REQUEST && allocations_seen != 0)) return 42;
        if (scenario == EXEC_REJECT_IGNORE) return 0;
        if (!self->interrupt_pending) return 43;
        api->yield(); return 44;
    }
    api->exec("next", args, envp); return 45;
}

static struct cb_task *find_task(cb_pid_t pid)
{
    struct cb_task *task;
    for (task = kernel->tasks; task != NULL; task = task->next)
        if (task->pid == pid) return task;
    return NULL;
}
static int controller(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    char *args[] = {"target", NULL};
    char data[64], byte;
    cb_pid_t pid, peer_pid = 0;
    int status, expected = 0;
    unsigned i;
    struct cb_task *task;
    enum cb_task_state state;
    (void)argc; (void)argv;
    parent_task = kernel->current;
    parent_payload = api->allocate(23);
    if (parent_payload == NULL || api->pipe(pipe_fds) != 0) return 10;
    parent_file = parent_task->descriptors[pipe_fds[0]].file;
    if (cb_kernel_request_interrupt(NULL, 1) != -CB_EINVAL ||
        cb_kernel_request_interrupt(kernel, -1) != -CB_ENOENT) return 11;
    if (scenario == WRITE_DEFAULT || scenario == WRITE_IGNORE) {
        memset(data, 'A', sizeof(data));
        for (i = 0; i < 4096 / sizeof(data); ++i)
            if (api->write(pipe_fds[1], data, sizeof(data)) != (cb_ssize_t)sizeof(data)) return 12;
    }
    if (scenario == PIPE_DEFAULT) {
        args[0] = "peer";
        if (api->pipe(peer_fds) != 0 ||
            api->spawn(args[0], args, envp, NULL, 0, &peer_pid) != 0) return 67;
        peer_task = find_task(peer_pid);
        if (peer_task == NULL) return 68;
        for (i = 0; i < 20 && peer_task->state != CB_TASK_BLOCKED_PIPE; ++i) api->yield();
        if (peer_task->state != CB_TASK_BLOCKED_PIPE || peer_payload == NULL) return 69;
        args[0] = "target";
    }
    if (api->spawn(args[0], args, envp, NULL, 0, &pid) != 0) return 13;
    task = find_task(pid);
    if (task == NULL) return 14;
    target_task = task;
    if (scenario == BEFORE_ENTRY) {
        if (cb_kernel_request_interrupt(kernel, pid) != 0) return 15;
    } else {
        /* Scheduler fairness is observed, not assumed from a single yield. */
        for (i = 0; i < 50 && !target_ready && task->state != CB_TASK_ZOMBIE; ++i) api->yield();
        if (scenario <= POLL_IGNORE || scenario == SHORT_EXECUTOR ||
            scenario == UNSET_EXECUTOR || scenario == UNKNOWN_EXECUTOR ||
            scenario == YIELD_DEFAULT || scenario == LIVE_DESTROY) {
            if (!target_ready) return 16;
            state = task->state;
            if (scenario <= POLL_IGNORE) {
                enum cb_task_state want = scenario < CONSOLE_DEFAULT ? CB_TASK_BLOCKED_PIPE :
                    scenario < WAIT_DEFAULT ? CB_TASK_BLOCKED_CONSOLE :
                    scenario < POLL_DEFAULT ? CB_TASK_BLOCKED_WAIT : CB_TASK_BLOCKED_POLL;
                if (state != want) return 17;
            }
            if (scenario == LIVE_DESTROY) {
                if (state != CB_TASK_RUNNABLE || target_payload == NULL) return 18;
                return 0; /* Host checks and destroys this live child, no termination. */
            }
            for (i = 0; i < 3; ++i) {
                int unsupported_case = scenario >= SHORT_EXECUTOR && scenario <= UNKNOWN_EXECUTOR;
                if (cb_kernel_request_interrupt(kernel, pid) != (unsupported_case ? -CB_ENOSYS : 0))
                    return 19;
            }
            if ((scenario <= POLL_IGNORE && (scenario & 1)) ||
                (scenario >= SHORT_EXECUTOR && scenario <= UNKNOWN_EXECUTOR)) {
                if (task->state != state || task->interrupt_pending) return 46;
                if (scenario == WRITE_IGNORE) {
                    if (api->read(pipe_fds[0], &byte, 1) != 1 || byte != 'A') return 47;
                } else if (scenario == CONSOLE_IGNORE) input_ready = 1;
                else if (scenario == WAIT_IGNORE) allow_leaf = 1;
                else if (scenario == POLL_IGNORE) now += 20;
                else if (api->write(pipe_fds[1], "Q", 1) != 1) return 48;
            } else if (task->state != CB_TASK_RUNNABLE || !task->interrupt_pending) return 49;
        }
    }
    allow_leaf = 1;
    if ((scenario <= POLL_IGNORE && !(scenario & 1)) || scenario == BEFORE_ENTRY ||
        scenario == YIELD_DEFAULT || scenario == EXEC_PENDING || scenario == EXEC_INSTALLED ||
        scenario == EXEC_REJECT_PENDING || scenario == LATE_EXEC_REQUEST || scenario == FAILED_EXEC ||
        scenario == SPAWN_PENDING) expected = 130;
    if (scenario == EARLY_EXIT) expected = 7;
    if (api->waitpid(pid, &status) != pid || status != expected) return 50;
    if (scenario == WRITE_IGNORE) {
        unsigned j;
        for (i = 0; i < 4096 / sizeof(data); ++i) {
            if (api->read(pipe_fds[0], data, sizeof(data)) != (cb_ssize_t)sizeof(data)) return 57;
            for (j = 0; j < sizeof(data); ++j)
                if (data[j] != (i * sizeof(data) + j == 4095 ? 'Q' : 'A')) return 58;
        }
    }
    if (terminated != 1 || error || target_payload != NULL) return error ? (int)error : 51;
    if (scenario == BEFORE_ENTRY && entered != 0) return 52;
    if ((scenario == EXEC_PENDING || scenario == EXEC_INSTALLED) &&
        (injected != 1 || next_entered != 0)) return 53;
    if ((scenario == IGNORE_EXEC || scenario == EXEC_UNSUPPORTED ||
         scenario == REQUEST_UNSUPPORTED_PENDING) && next_entered != 1) return 54;
    if (scenario == SPAWN_PENDING || scenario == SPAWN_IGNORE) {
        for (i = 0; i < 20 && next_entered == 0; ++i) api->yield();
        if (next_entered != 1) return 55;
    }
    if (peer_task != NULL) {
        if (peer_task->state != CB_TASK_BLOCKED_PIPE || peer_payload == NULL) return 79;
        if (api->write(peer_fds[1], "P", 1) != 1 || api->waitpid(peer_pid, &status) != peer_pid ||
            status != 0 || peer_payload != NULL) return 80;
        peer_task = NULL;
    }
    if (cb_kernel_request_interrupt(kernel, pid) != -CB_ENOENT) return 56;
    return 0;
}

int cb_signal_probe(const struct cb_host_ops_v1 *host)
{
    struct cb_host_ops_v1 copy = *host;
    const struct cb_program_v1 parent = {
        CB_ABI_VERSION_V1, sizeof(parent), "sh", 0, 64 * 1024, controller
    }, child = {
        CB_ABI_VERSION_V1, sizeof(child), "target", 0, 64 * 1024, target
    }, next_program = {
        CB_ABI_VERSION_V1, sizeof(next_program), "next", 0, 64 * 1024, next
    }, leaf_program = {
        CB_ABI_VERSION_V1, sizeof(leaf_program), "leaf", 0, 64 * 1024, leaf
    }, peer_program = {
        CB_ABI_VERSION_V1, sizeof(peer_program), "peer", 0, 64 * 1024, peer
    }, inherited_program = {
        CB_ABI_VERSION_V1, sizeof(inherited_program), "inherited", 0, 64 * 1024, inherited
    };
    /* Frozen pre-capability executor layout, actually allocated at old size. */
    struct old_executor {
        uint32_t abi_version, struct_size;
        int (*prepare)(struct cb_kernel *, const struct cb_executor_ops *, const void *, struct cb_program **);
        struct cb_execution *(*create)(struct cb_task *, const struct cb_program *);
        void (*resume)(struct cb_execution *);
        void (*suspend)(struct cb_execution *);
        void (*terminate)(struct cb_execution *);
        void (*destroy)(struct cb_execution *);
        void (*program_destroy)(struct cb_kernel *, struct cb_program *);
    };
    struct cb_executor_ops *short_ops;
    int result = 0;
    base = host;
    copy.allocate = allocate; copy.release = release;
    copy.context_root = context_root; copy.context_create = context_create;
    copy.context_switch = context_switch; copy.context_destroy = context_destroy;
    copy.console_poll = console_poll; copy.console_read = console_read;
    copy.console_write = console_write; copy.monotonic_millis = clock_now;
    copy.yield_host = host_yield;
    supported = (struct cb_executor_ops){CB_ABI_VERSION_V1, sizeof(supported),
        prepare, create, resume, suspend, terminate, destroy, program_destroy,
        CB_EXECUTOR_COOPERATIVE_INTERRUPT};
    for (scenario = 0; scenario < CASE_COUNT && result == 0; ++scenario) {
        const struct cb_executor_ops *child_ops, *next_ops;
        error = budget = context_count = root_calls = entered = next_entered = terminated = 0;
        payloads_freed = injected = allocations_seen = fail_allocation = allocation_armed = 0;
        parent_task = target_task = peer_task = NULL;
        parent_payload = target_payload = peer_payload = NULL;
        parent_file = NULL; root = current_context = created_context = NULL;
        allow_leaf = input_ready = target_ready = 0; now = 1000;
        unsupported = supported; unsupported.capabilities = scenario == UNKNOWN_EXECUTOR ? 2 : 0;
        short_ops = base->allocate(sizeof(struct old_executor));
        if (short_ops == NULL) return 91;
        memcpy(short_ops, &supported, sizeof(struct old_executor));
        short_ops->struct_size = sizeof(struct old_executor);
        child_ops = scenario == SHORT_EXECUTOR ? short_ops :
                    scenario == UNSET_EXECUTOR || scenario == UNKNOWN_EXECUTOR ? &unsupported : &supported;
        next_ops = scenario >= EXEC_REJECT_PENDING && scenario <= LATE_EXEC_REQUEST ? &unsupported : &supported;
        kernel = cb_kernel_create(&copy);
        if (kernel == NULL) { base->release(short_ops); return 92; }
        if (cb_kernel_register(kernel, &parent) != 0 ||
            cb_kernel_register_executor(kernel, child_ops, &child) != 0 ||
            cb_kernel_register_executor(kernel, next_ops, &next_program) != 0 ||
            cb_kernel_register(kernel, &leaf_program) != 0 ||
            cb_kernel_register(kernel, &peer_program) != 0 ||
            cb_kernel_register(kernel, &inherited_program) != 0 || cb_kernel_boot(kernel, NULL) != 0)
            result = 93;
        else result = cb_kernel_run(kernel);
        if (result == 0 && scenario == LIVE_DESTROY &&
            (target_payload == NULL || target_task->allocations == NULL || terminated != 0)) result = 94;
        cb_kernel_destroy(kernel); kernel = NULL;
        base->release(short_ops);
        if (result == 0 && (context_count != 0 || root_calls != 1 || parent_payload != NULL ||
            target_payload != NULL || peer_payload != NULL || error)) result = error ? (int)error : 95;
        if (result != 0) return (int)scenario * 100 + result;
    }
    return 0;
}
