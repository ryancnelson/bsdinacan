#include "internal.h"
#include <stddef.h>
#include <errno.h>

struct _list {
    struct _list *next;
    int payload;
};

struct _list *cb_tee_head = NULL;
int cb_tee_main = 0;
void cb_tee_add(int fd) { (void)fd; }

struct cb_tee_execution {
    struct cb_execution common;
    struct cb_execution *inner_execution;
    struct _list *task_head;
};

static int sidecar_live = 0;

static const struct cb_host_ops_v1 *base_host;
static int alloc_count = 0, target_alloc_fail = 0;
static int context_count = 0, target_context_fail = 0, live_contexts = 0;
static int live_allocs = 0, context_roots = 0;

static void *test_allocate(size_t size) {
    ++alloc_count;
    if (target_alloc_fail > 0 && target_alloc_fail == alloc_count) return NULL;
    void *p = base_host->allocate(size);
    if (p) ++live_allocs;
    return p;
}
static void test_release(void *p) {
    if (p) --live_allocs;
    base_host->release(p);
}
static struct cb_host_context *test_context_root(void) {
    ++context_roots;
    return base_host->context_root();
}
static struct cb_host_context *test_context_create(void (*entry)(void *), void *arg, size_t size) {
    ++context_count;
    if (target_context_fail > 0 && target_context_fail == context_count) return NULL;
    struct cb_host_context *ctx = base_host->context_create(entry, arg, size);
    if (ctx) ++live_contexts;
    return ctx;
}
static void test_context_destroy(struct cb_host_context *ctx) {
    if (ctx == base_host->context_root()) {
        --context_roots;
    } else {
        --live_contexts;
    }
    base_host->context_destroy(ctx);
}

static int fail_sidecar_alloc = 0;
static int fail_native_alloc = 0;
static int fail_context_create = 0;

static int tee_wrapper_prepare(struct cb_kernel *kernel,
                               const struct cb_executor_ops *executor,
                               const void *source,
                               struct cb_program **program_out)
{
    return cb_native_executor()->prepare(kernel, executor, source, program_out);
}

static struct cb_execution *tee_wrapper_instance_create(
    struct cb_task *task, const struct cb_program *program)
{
    struct cb_tee_execution *wrapper;
    
    if (fail_sidecar_alloc) {
        target_alloc_fail = alloc_count + 1;
        fail_sidecar_alloc = 0;
    }
    
    wrapper = cb_allocate(task->kernel, sizeof(*wrapper));
    if (wrapper == NULL) return NULL;
    
    if (fail_native_alloc) {
        target_alloc_fail = alloc_count + 1;
        fail_native_alloc = 0;
    }
    if (fail_context_create) {
        target_context_fail = context_count + 1;
        fail_context_create = 0;
    }
    
    wrapper->inner_execution = cb_native_executor()->instance_create(task, program);
    if (wrapper->inner_execution == NULL) {
        cb_release(task->kernel, wrapper);
        return NULL;
    }
    
    ++sidecar_live;
    wrapper->common.executor = program->executor;
    wrapper->common.task = task;
    wrapper->common.program = program;
    wrapper->task_head = NULL;
    
    return &wrapper->common;
}

static void tee_wrapper_start_or_resume(struct cb_execution *execution)
{
    struct cb_tee_execution *wrapper = (struct cb_tee_execution *)execution;
    cb_tee_head = wrapper->task_head;
    cb_native_executor()->start_or_resume(wrapper->inner_execution);
    if (wrapper->common.task->state != CB_TASK_ZOMBIE &&
        wrapper->common.task->state != CB_TASK_DEAD) {
        wrapper->task_head = cb_tee_head;
    }
    cb_tee_head = NULL;
}

static void tee_wrapper_suspend(struct cb_execution *execution)
{
    struct cb_tee_execution *wrapper = (struct cb_tee_execution *)execution;
    cb_native_executor()->suspend(wrapper->inner_execution);
}

static void tee_wrapper_request_termination(struct cb_execution *execution)
{
    struct cb_tee_execution *wrapper = (struct cb_tee_execution *)execution;
    cb_native_executor()->request_termination(wrapper->inner_execution);
}

static void tee_wrapper_instance_destroy(struct cb_execution *execution)
{
    struct cb_tee_execution *wrapper = (struct cb_tee_execution *)execution;
    struct cb_kernel *kernel = wrapper->common.task->kernel;
    --sidecar_live;
    cb_native_executor()->instance_destroy(wrapper->inner_execution);
    cb_release(kernel, wrapper);
}

static void tee_wrapper_program_destroy(struct cb_kernel *kernel,
                                        struct cb_program *program)
{
    cb_native_executor()->program_destroy(kernel, program);
}

static const struct cb_executor_ops tee_wrapper_ops = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_executor_ops),
    tee_wrapper_prepare,
    tee_wrapper_instance_create,
    tee_wrapper_start_or_resume,
    tee_wrapper_suspend,
    tee_wrapper_request_termination,
    tee_wrapper_instance_destroy,
    tee_wrapper_program_destroy
};

static int payload_allocated = 0;
static int fail_spawn_sidecar = 0;
static int live_teardown_test = 0;

static int entry_mock(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct _list *node;
    int is_child = (argc > 1 && argv[1][0] == 'c');
    
    if (cb_tee_head != NULL) { cb_tee_main = 1; return 1; }
    
    node = api->allocate(sizeof(*node));
    if (node == NULL) { cb_tee_main = 2; return 2; }
    ++payload_allocated;
    
    node->payload = is_child ? 99 : 42;
    node->next = cb_tee_head;
    cb_tee_head = node;
    
    if (!is_child) {
        char *spawn_args[] = {"mock_tee", "child", NULL};
        cb_pid_t pid;
        int status = 0;
        
        if (fail_spawn_sidecar) {
            fail_sidecar_alloc = 1;
            fail_spawn_sidecar = 0;
        }
        
        if (api->spawn("mock_tee", spawn_args, envp, NULL, 0, &pid) != 0) { 
            if (cb_tee_head != node) { cb_tee_main = 7; return 7; }
            cb_tee_main = 3; return 3; 
        }
        api->yield();
        
        if (live_teardown_test) {
            /* Exit without waiting for child. Child remains blocked/runnable. */
            cb_tee_head = node->next; /* Restore head since we're exiting cleanly? Actually, wrapper does it. */
            return 99;
        }
        
        if (api->waitpid(pid, &status) != pid) { cb_tee_main = 6; return 6; }
        if (status != 0) { cb_tee_main = 4; return 4; }
    } else {
        if (live_teardown_test) {
            /* Child yields forever to stay alive during teardown */
            for (;;) api->yield();
        }
    }
    
    api->yield();
    
    if (cb_tee_head != node || cb_tee_head->payload != (is_child ? 99 : 42)) { cb_tee_main = 5; return 5; }
    
    return 0;
}

static int entry_mock_exec(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct _list *node;
    char *bad_args[] = {"nonexistent", NULL};
    char *good_args[] = {"mock_peer", NULL};
    (void)argc; (void)argv;
    
    if (cb_tee_head != NULL) { cb_tee_main = 1; return 1; }
    node = api->allocate(sizeof(*node));
    if (node == NULL) { cb_tee_main = 2; return 2; }
    ++payload_allocated;
    
    node->payload = 42;
    node->next = cb_tee_head;
    cb_tee_head = node;
    
    if (api->exec(bad_args[0], bad_args, envp) != -1) { cb_tee_main = 8; return 8; }
    if (api->get_errno() != CB_ENOENT) { cb_tee_main = 9; return 9; }
    
    if (cb_tee_head != node || cb_tee_head->payload != 42) { cb_tee_main = 4; return 4; }
    
    api->exec(good_args[0], good_args, envp);
    cb_tee_main = 5;
    return 5;
}

static int entry_peer(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    (void)api; (void)argc; (void)argv; (void)envp;
    if (cb_tee_head != NULL) { cb_tee_main = 11; return 11; }
    return 0;
}

static const struct cb_program_v1 mock_tee = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "mock_tee", 0, 64 * 1024, entry_mock
};
static const struct cb_program_v1 mock_tee_exec = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "mock_tee_exec", 0, 64 * 1024, entry_mock_exec
};
static const struct cb_program_v1 mock_peer = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "mock_peer", 0, 64 * 1024, entry_peer
};

int cb_tee_state_probe(const struct cb_host_ops_v1 *host)
{
    struct cb_kernel *kernel;
    struct cb_host_ops_v1 copy = *host;
    int r1, r2;
    
    base_host = host;
    copy.allocate = test_allocate;
    copy.release = test_release;
    copy.context_root = test_context_root;
    copy.context_create = test_context_create;
    copy.context_destroy = test_context_destroy;
    
    alloc_count = target_alloc_fail = 0;
    context_count = target_context_fail = live_contexts = context_roots = 0;
    live_allocs = sidecar_live = payload_allocated = 0;

    kernel = cb_kernel_create(&copy);
    if (!kernel) return -1;
    cb_register_base_programs(kernel);

    /* Phase 1: Prove behavioral red sequentially */
    cb_tee_head = NULL; cb_tee_main = 0;
    if (cb_kernel_register_executor(kernel, cb_native_executor(), &mock_tee) != 0) return -10;
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -11;
    r1 = cb_kernel_run(kernel);
    if (r1 != 4 || cb_tee_main != 4) { cb_kernel_destroy(kernel); return 99; }
    cb_kernel_destroy(kernel);
    if (live_allocs != 0) return 101; 

    /* Phase 1b: Sequential leak */
    cb_tee_main = 0;
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    if (cb_kernel_register_executor(kernel, cb_native_executor(), &mock_tee) != 0) return -12;
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -13;
    r2 = cb_kernel_run(kernel); 
    cb_kernel_destroy(kernel);
    if (r2 != 1 || cb_tee_main != 1) return 100;
    cb_tee_head = NULL; 

    /* Phase 2: Isolated Interleaved & Lifecycle Test */
    cb_tee_main = 0;
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    if (cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee) != 0) return -20;
    
    payload_allocated = 0;
    
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -23;
    if (cb_kernel_run(kernel) != 0) return 200; 
    if (cb_tee_main != 0) return 201; 
    if (cb_tee_head != NULL) return 202; 
    
    if (payload_allocated != 2) return 203; 
    /* The boot task (parent) is in ZOMBIE state. Its node was freed by task_release_allocations, 
       but its sidecar and context remain alive until instance_destroy. */
    if (sidecar_live != 1) return 204;
    if (live_contexts != 1) return 205;
    
    /* Assert payload release observation: if the two nodes weren't freed, live_allocs would not drop correctly.
       After cb_kernel_destroy, all kernel structures are wiped, so live_allocs must be exactly 0. */
    cb_kernel_destroy(kernel);
    if (sidecar_live != 0 || live_contexts != 0 || live_allocs != 0) return 206;

    /* Phase 3: Explicit Finite Injections */
    /* Inject sidecar allocation failure */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_peer);
    fail_sidecar_alloc = 1;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 301;
    if (cb_tee_head != NULL) return 302;
    cb_kernel_destroy(kernel);
    
    /* Inject native allocation failure */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_peer);
    fail_native_alloc = 1;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 303;
    if (cb_tee_head != NULL) return 304;
    cb_kernel_destroy(kernel);
    
    /* Inject host context failure */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_peer);
    fail_context_create = 1;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 305;
    if (cb_tee_head != NULL) return 306;
    cb_kernel_destroy(kernel);

    /* Phase 3b: Active Peer with non-NULL global list during failed creation */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee);
    cb_tee_main = 0;
    fail_spawn_sidecar = 1; /* Instruct mock_tee parent to trigger fail_sidecar_alloc during api->spawn */
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return 310;
    if (cb_kernel_run(kernel) != 3) return 311;
    if (cb_tee_main != 3) return 312; /* Assert spawn failed and cb_tee_head was correctly preserved! */
    cb_kernel_destroy(kernel);

    /* Phase 3c: Suspended live-kernel teardown */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee);
    cb_tee_main = 0;
    live_teardown_test = 1;
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return 320;
    if (cb_kernel_run(kernel) != 99) return 321;
    
    /* Before teardown, child task is STILL ALIVE (its sidecar and context exist).
       Boot task is ZOMBIE (sidecar/context exist). */
    if (sidecar_live != 2) return 322;
    if (live_contexts != 2) return 323;
    
    /* Now destroy kernel with live task! */
    cb_kernel_destroy(kernel);
    live_teardown_test = 0;
    if (sidecar_live != 0 || live_contexts != 0 || live_allocs != 0) return 324;

    /* Phase 4: Exec tests */
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee_exec);
    cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_peer);
    
    if (cb_kernel_boot(kernel, "mock_tee_exec") != 0) return -30;
    if (cb_kernel_run(kernel) != 0) return 400;
    if (sidecar_live != 1) return 401;

    cb_kernel_destroy(kernel);
    if (live_allocs != 0) return 500;
    
    return 0;
}
