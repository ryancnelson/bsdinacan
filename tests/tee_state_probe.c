#include "internal.h"
#include <string.h>

/* Synthetic tee-shaped global; no upstream tee source is imported here. */
struct _list { struct _list *next; int value; };
static struct _list *head;
struct tee_execution {
    struct cb_execution common;
    struct cb_execution *inner;
    struct _list *saved;
};

enum { NONE, SIDECAR, INNER, PAYLOAD, BOOKKEEPING, KINDS };
enum { NO_FAILURE, FAIL_SIDECAR, FAIL_INNER, FAIL_CONTEXT };
struct allocation { void *pointer; struct cb_task *owner; unsigned kind; };
static struct allocation allocations[192];
static struct cb_host_context *contexts[16], *root;
static const struct cb_host_ops_v1 *base;
static struct cb_kernel *test_kernel;
static struct cb_task *allocation_owner, *running_owner, *held_task;
static unsigned allocation_count, made[KINDS], freed[KINDS], phase, payload_part;
static unsigned context_live, root_calls, root_frees, observation_error;
static unsigned failure, failure_hits[4], entered, term_checked, exec_checked;
static unsigned destroy_checked, hold_ready;

static unsigned live(unsigned kind) { return made[kind] - freed[kind]; }
static unsigned owned(struct cb_task *task, unsigned kind)
{
    unsigned i, n = 0;
    for (i = 0; i < allocation_count; ++i)
        if (allocations[i].pointer != NULL && allocations[i].owner == task &&
            allocations[i].kind == kind) ++n;
    return n;
}
static void *allocate(size_t size)
{
    unsigned kind = phase;
    void *p;
    if ((failure == FAIL_SIDECAR && phase == SIDECAR) ||
        (failure == FAIL_INNER && phase == INNER)) {
        ++failure_hits[failure];
        return NULL;
    }
    p = base->allocate(size);
    if (p == NULL || phase == NONE) return p;
    if (phase == PAYLOAD) kind = payload_part++ == 0 ? PAYLOAD : BOOKKEEPING;
    if (allocation_count == sizeof(allocations)/sizeof(allocations[0])) {
        observation_error = 60;
        base->release(p);
        return NULL;
    }
    allocations[allocation_count].pointer = p;
    allocations[allocation_count].owner = allocation_owner;
    allocations[allocation_count++].kind = kind;
    ++made[kind];
    return p;
}
static void release(void *p)
{
    unsigned i;
    if (p != NULL) for (i = 0; i < allocation_count; ++i) {
        if (allocations[i].pointer == p) {
            ++freed[allocations[i].kind];
            allocations[i].pointer = NULL;
            break;
        }
    }
    base->release(p);
}
static void remember_context(struct cb_host_context *p)
{
    unsigned i;
    if (p == NULL) return;
    for (i = 0; i < sizeof(contexts)/sizeof(contexts[0]); ++i)
        if (contexts[i] == NULL) { contexts[i] = p; ++context_live; return; }
    observation_error = 61;
}
static struct cb_host_context *context_root(void)
{
    struct cb_host_context *p = base->context_root();
    ++root_calls;
    if (root != NULL) observation_error = 62;
    root = p;
    remember_context(p);
    return p;
}
static struct cb_host_context *context_create(void (*entry)(void *), void *arg, size_t size)
{
    struct cb_host_context *p;
    if (phase == INNER && failure == FAIL_CONTEXT) {
        ++failure_hits[FAIL_CONTEXT];
        return NULL;
    }
    p = base->context_create(entry, arg, size);
    remember_context(p);
    return p;
}
static void context_destroy(struct cb_host_context *p)
{
    unsigned i;
    if (p == NULL) return;
    for (i = 0; i < sizeof(contexts)/sizeof(contexts[0]); ++i)
        if (contexts[i] == p) { contexts[i] = NULL; --context_live; break; }
    if (i == sizeof(contexts)/sizeof(contexts[0])) observation_error = 63;
    if (p == root) { root = NULL; ++root_frees; }
    base->context_destroy(p);
}
static cb_ssize_t console_write(int stream, const void *p, size_t n)
{
    (void)stream; (void)p;
    return (cb_ssize_t)n;
}
static int prepare(struct cb_kernel *kernel, const struct cb_executor_ops *ops,
                   const void *source, struct cb_program **out)
{
    return cb_native_executor()->prepare(kernel, ops, source, out);
}
static struct cb_execution *instance_create(struct cb_task *task,
                                            const struct cb_program *program)
{
    struct tee_execution *e;
    allocation_owner = task;
    phase = SIDECAR;
    e = cb_allocate(task->kernel, sizeof(*e));
    phase = NONE;
    if (e == NULL) return NULL;
    phase = INNER;
    e->inner = cb_native_executor()->instance_create(task, program);
    phase = NONE;
    if (e->inner == NULL) { cb_release(task->kernel, e); return NULL; }
    e->common.executor = program->executor;
    e->common.task = task;
    e->common.program = program;
    e->saved = NULL;
    return &e->common;
}
static void start_or_resume(struct cb_execution *common)
{
    struct tee_execution *e = (struct tee_execution *)common;
    head = e->saved;
    running_owner = common->task;
    cb_native_executor()->start_or_resume(e->inner);
    if (common->task->state != CB_TASK_ZOMBIE && common->task->state != CB_TASK_DEAD)
        e->saved = head;
    head = NULL;
    running_owner = NULL;
}
static void suspend(struct cb_execution *common)
{
    struct tee_execution *e = (struct tee_execution *)common;
    cb_native_executor()->suspend(e->inner);
}
static void request_termination(struct cb_execution *common)
{
    struct tee_execution *e = (struct tee_execution *)common;
    /* api_exit must already have reclaimed task payload AND bookkeeping.
       Reap/kernel_destroy are deliberately too late to satisfy this check. */
    if (owned(common->task, PAYLOAD) || owned(common->task, BOOKKEEPING))
        observation_error = 65;
    ++term_checked;
    cb_native_executor()->request_termination(e->inner);
}
static void instance_destroy(struct cb_execution *common)
{
    struct tee_execution *e = (struct tee_execution *)common;
    if (common->task->state == CB_TASK_EXEC_PENDING) {
        /* Successful exec destroys the old execution before freeing its heap. */
        if (owned(common->task, PAYLOAD) != 1 || owned(common->task, BOOKKEEPING) != 1)
            observation_error = 66;
        ++exec_checked;
    } else {
        if (owned(common->task, PAYLOAD) || owned(common->task, BOOKKEEPING))
            observation_error = 67;
        ++destroy_checked;
    }
    cb_native_executor()->instance_destroy(e->inner);
    cb_release(common->task->kernel, e);
}
static void program_destroy(struct cb_kernel *kernel, struct cb_program *program)
{
    cb_native_executor()->program_destroy(kernel, program);
}
static const struct cb_executor_ops wrapper = {
    CB_ABI_VERSION_V1, sizeof(wrapper), prepare, instance_create, start_or_resume,
    suspend, request_termination, instance_destroy, program_destroy, 0
};

static int spawn_wait(const struct cb_api_v1 *api, const char *program,
                       const char *mode, char *const envp[], int expected)
{
    char *args[] = {(char *)program, (char *)mode, NULL};
    cb_pid_t pid;
    int status;
    if (api->spawn(program, args, envp, NULL, 0, &pid) != 0) return -1;
    return api->waitpid(pid, &status) == pid && status == expected ? 0 : -1;
}
static int native_peer(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    (void)argc; (void)argv; (void)envp;
    if (head != NULL || running_owner != NULL ||
        owned(test_kernel->current, PAYLOAD) || owned(test_kernel->current, BOOKKEEPING)) return 21;
    api->yield();
    return head == NULL && running_owner == NULL ? 0 : 22;
}
static int tee_entry(const struct cb_api_v1 *api, int argc,
                     char *const argv[], char *const envp[])
{
    struct _list *node;
    struct cb_task *owner = running_owner;
    unsigned i;
    const char *mode = argc == 2 ? argv[1] : "";
    ++entered;
    if (head != NULL || owner == NULL || owned(owner, PAYLOAD) || owned(owner, BOOKKEEPING)) return 10;
    allocation_owner = owner; payload_part = 0; phase = PAYLOAD;
    node = api->allocate(sizeof(*node));
    phase = NONE;
    if (node == NULL || payload_part != 2) return 11;
    node->next = NULL; node->value = 42; head = node;
    if (strcmp(mode, "failures") == 0) {
        char *args[] = {(char *)"teemock", (char *)"leaf", NULL};
        for (i = FAIL_SIDECAR; i <= FAIL_CONTEXT; ++i) {
            unsigned before[KINDS], j, before_contexts = context_live, before_entries = entered;
            cb_pid_t pid = -1;
            int result, error;
            for (j = SIDECAR; j < KINDS; ++j) before[j] = live(j);
            failure = i;
            result = api->spawn("teemock", args, envp, NULL, 0, &pid);
            error = api->get_errno(); failure = NO_FAILURE;
            if (result != -1 || error != CB_ENOMEM || failure_hits[i] != 1 ||
                entered != before_entries || context_live != before_contexts ||
                head != node || node->value != 42 || running_owner != owner) return 12;
            for (j = SIDECAR; j < KINDS; ++j)
                if (live(j) != before[j]) return 13;
        }
    } else if (strcmp(mode, "isolate") == 0) {
        char *args[] = {(char *)"teemock", (char *)"leaf", NULL};
        cb_pid_t pid;
        int status;
        if (api->spawn("teemock", args, envp, NULL, 0, &pid) != 0) return 14;
        if (spawn_wait(api, "nativepeer", "", envp, 0) != 0) return 15;
        if (api->waitpid(pid, &status) != pid || status != 0) return 16;
        if (owned(owner, PAYLOAD) != 1 || live(PAYLOAD) != 1) return 17;
    } else if (strcmp(mode, "nonzero") == 0) {
        api->exit(17);
        return 18;
    } else if (strcmp(mode, "execwrap") == 0 || strcmp(mode, "execnative") == 0) {
        char *bad[] = {(char *)"missing-tee-target", NULL};
        char *good[] = {(char *)(strcmp(mode, "execwrap") == 0 ? "teemock" : "nativepeer"),
                        (char *)"fresh", NULL};
        if (api->exec(bad[0], bad, envp) != -1 || api->get_errno() != CB_ENOENT ||
            head != node || node->value != 42 || owned(owner, PAYLOAD) != 1) return 19;
        api->exec(good[0], good, envp);
        return 20;
    } else if (strcmp(mode, "hold") == 0) {
        held_task = owner; hold_ready = 1;
        for (i = 0; i < 32; ++i) api->yield();
        return 23; /* Host must regain control and destroy us while suspended. */
    }
    for (i = 0; i < 3; ++i) {
        api->yield();
        if (head != node || node->value != 42 || running_owner != owner) return 24;
    }
    return 0;
}
static int orchestrator(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    static const char *const modes[] = {"isolate", "leaf", "leaf", "nonzero",
                                       "execwrap", "execnative", "failures"};
    unsigned i;
    if (argc == 2 && strcmp(argv[1], "leave") == 0) {
        char *args[] = {(char *)"teemock", (char *)"hold", NULL};
        cb_pid_t pid;
        if (api->spawn("teemock", args, envp, NULL, 0, &pid) != 0) return 30;
        for (i = 0; i < 16 && !hold_ready; ++i) api->yield();
        return hold_ready && head == NULL && running_owner == NULL ? 0 : 31;
    }
    for (i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i) {
        if (spawn_wait(api, "teemock", modes[i], envp, i == 3 ? 17 : 0) != 0)
            return 32 + (int)i;
        if (head != NULL || running_owner != NULL || live(PAYLOAD) || live(BOOKKEEPING) ||
            live(SIDECAR) || live(INNER) || context_live != 3) return 40 + (int)i;
    }
    return 0;
}
static const struct cb_program_v1 tee_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "teemock", 0, 64 * 1024, tee_entry
};
static const struct cb_program_v1 peer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "nativepeer", 0, 64 * 1024, native_peer
};
static const struct cb_program_v1 driver_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "teedriver", 0, 64 * 1024, orchestrator
};

/* Host/root stack only: each temporary kernel is closed before the next starts. */
int cb_tee_state_probe(const struct cb_host_ops_v1 *host)
{
    struct cb_host_ops_v1 copy = *host;
    unsigned scenario, kind;
    int result = 0;
    memset(allocations, 0, sizeof(allocations));
    memset(contexts, 0, sizeof(contexts));
    memset(made, 0, sizeof(made)); memset(freed, 0, sizeof(freed));
    memset(failure_hits, 0, sizeof(failure_hits));
    base = host; root = NULL; head = NULL;
    running_owner = held_task = allocation_owner = NULL;
    allocation_count = phase = payload_part = context_live = root_calls = root_frees = 0;
    observation_error = failure = entered = term_checked = exec_checked = destroy_checked = hold_ready = 0;
    copy.allocate = allocate; copy.release = release;
    copy.context_root = context_root; copy.context_create = context_create;
    copy.context_destroy = context_destroy; copy.console_write = console_write;
    for (scenario = 0; scenario < 2 && result == 0; ++scenario) {
        test_kernel = cb_kernel_create(&copy);
        if (test_kernel == NULL) return 50;
        cb_register_base_programs(test_kernel);
        if (cb_kernel_register_executor(test_kernel, &wrapper, &tee_program) != 0 ||
            cb_kernel_register(test_kernel, &peer_program) != 0 ||
            cb_kernel_register(test_kernel, &driver_program) != 0 ||
            cb_kernel_boot(test_kernel, scenario ? "teedriver leave" : "teedriver") != 0)
            result = 51;
        else result = cb_kernel_run(test_kernel);
        if (result == 0 && (head != NULL || running_owner != NULL || observation_error))
            result = observation_error ? (int)observation_error : 52;
        if (result == 0 && scenario == 0) {
            if (live(PAYLOAD) || live(BOOKKEEPING) || live(SIDECAR) || live(INNER) ||
                context_live != 2 || exec_checked != 2 || term_checked != 7 || entered != 9)
                result = 53;
        } else if (result == 0) {
            if (!hold_ready || held_task == NULL || held_task->state != CB_TASK_RUNNABLE ||
                owned(held_task, PAYLOAD) != 1 || owned(held_task, BOOKKEEPING) != 1 ||
                live(SIDECAR) != 1 || live(INNER) != 1 || context_live != 3)
                result = 54;
        }
        cb_kernel_destroy(test_kernel); test_kernel = NULL;
        for (kind = SIDECAR; kind < KINDS; ++kind)
            if (live(kind) != 0) result = 55;
        if (context_live || root != NULL || observation_error || root_calls != scenario + 1 ||
            root_frees != root_calls) result = observation_error ? (int)observation_error : 56;
    }
    if (result == 0 && (failure_hits[FAIL_SIDECAR] != 1 || failure_hits[FAIL_INNER] != 1 ||
        failure_hits[FAIL_CONTEXT] != 1 || destroy_checked != 8)) result = 57;
    return result;
}
