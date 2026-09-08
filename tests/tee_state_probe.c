#include "internal.h"
#include <stddef.h>

/* Synthetic list structure */
struct _list {
    struct _list *next;
    int payload;
};

/* The synthetic global pointer for tee */
struct _list *cb_tee_head = NULL;
int cb_tee_main = 0;
void cb_tee_add(int fd) { (void)fd; }

/* The wrapper sidecar execution structure */
struct cb_tee_execution {
    struct cb_execution common;
    struct cb_execution *inner_execution;
    struct _list *task_head;
};

static int tee_wrapper_prepare(struct cb_kernel *kernel,
                               const struct cb_executor_ops *executor,
                               const void *source,
                               struct cb_program **program_out)
{
    return cb_native_executor()->prepare(kernel, executor, source, program_out);
}

static struct cb_execution *tee_wrapper_instance_create(
    struct cb_task *task, struct cb_program *program)
{
    struct cb_tee_execution *wrapper;
    
    wrapper = cb_allocate(cb_task_kernel(task), sizeof(*wrapper));
    if (wrapper == NULL) return NULL;
    
    wrapper->inner_execution = cb_native_executor()->instance_create(task, program);
    if (wrapper->inner_execution == NULL) {
        cb_release(cb_task_kernel(task), wrapper);
        return NULL;
    }
    
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
    
    if (cb_task_state(wrapper->common.task) != CB_TASK_ZOMBIE &&
        cb_task_state(wrapper->common.task) != CB_TASK_DEAD) {
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
    struct cb_kernel *kernel = cb_task_kernel(wrapper->common.task);
    
    cb_native_executor()->instance_destroy(wrapper->inner_execution);
    cb_release(kernel, wrapper);
}

static void tee_wrapper_program_destroy(struct cb_kernel *kernel,
                                        struct cb_program *program)
{
    cb_native_executor()->program_destroy(kernel, program);
}

static const struct cb_executor_ops tee_wrapper_ops = {
    tee_wrapper_prepare,
    tee_wrapper_instance_create,
    tee_wrapper_start_or_resume,
    tee_wrapper_suspend,
    tee_wrapper_request_termination,
    tee_wrapper_instance_destroy,
    tee_wrapper_program_destroy
};

/* --- Mocks and Tests --- */

static int entry_mock(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct _list *node;
    (void)argc; (void)argv; (void)envp;
    if (cb_tee_head != NULL) return 1;
    node = api->allocate(sizeof(*node));
    if (node == NULL) return 2;
    node->payload = 42;
    node->next = cb_tee_head;
    cb_tee_head = node;
    api->write(-1, NULL, 0); /* Yield */
    if (cb_tee_head != node || cb_tee_head->payload != 42) return 3;
    return 0;
}

static int entry_mock_exec(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct _list *node;
    char *bad_args[] = {"nonexistent", NULL};
    char *good_args[] = {"mock_peer", NULL};
    (void)argc; (void)argv;
    if (cb_tee_head != NULL) return 1;
    node = api->allocate(sizeof(*node));
    if (node == NULL) return 2;
    node->payload = 42;
    node->next = cb_tee_head;
    cb_tee_head = node;
    
    /* failed exec */
    api->exec(bad_args[0], bad_args, envp);
    
    if (cb_tee_head != node || cb_tee_head->payload != 42) return 4;
    
    /* successful exec */
    api->exec(good_args[0], good_args, envp);
    return 5;
}

static int entry_peer(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    (void)api; (void)argc; (void)argv; (void)envp;
    if (cb_tee_head != NULL) return 11;
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

static const struct cb_host_ops_v1 *base_host;
static int alloc_count, target_alloc_fail;
static int context_count, target_context_fail;
static int live_allocs, max_live_allocs;

static void *test_allocate(size_t size) {
    ++alloc_count;
    if (target_alloc_fail == alloc_count) return NULL;
    void *p = base_host->allocate(size);
    if (p) {
        ++live_allocs;
        if (live_allocs > max_live_allocs) max_live_allocs = live_allocs;
    }
    return p;
}
static void test_release(void *p) {
    if (p) --live_allocs;
    base_host->release(p);
}
static struct cb_host_context *test_context_root(void) {
    return base_host->context_root();
}
static struct cb_host_context *test_context_create(void (*entry)(void *), void *arg, size_t size) {
    ++context_count;
    if (target_context_fail == context_count) return NULL;
    return base_host->context_create(entry, arg, size);
}
static void test_context_destroy(struct cb_host_context *ctx) {
    base_host->context_destroy(ctx);
}

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
    context_count = target_context_fail = 0;
    live_allocs = max_live_allocs = 0;

    kernel = cb_kernel_create(&copy);
    if (!kernel) return -1;
    cb_register_base_programs(kernel);

    /* Phase 1: Prove behavioral red with native executor sharing state */
    cb_tee_head = NULL;
    if (cb_kernel_register_executor(kernel, cb_native_executor(), &mock_tee) != 0) return -10;
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -11;
    r1 = cb_kernel_run(kernel); 
    
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -12;
    r2 = cb_kernel_run(kernel); 
    
    if (r1 != 0 || r2 != 1) { cb_kernel_destroy(kernel); return 100; }
    
    cb_tee_head = NULL; 
    cb_kernel_destroy(kernel);
    if (live_allocs != 0) return 101; 

    /* Phase 2: Isolated Wrapper Tests */
    alloc_count = target_alloc_fail = context_count = target_context_fail = live_allocs = max_live_allocs = 0;
    kernel = cb_kernel_create(&copy);
    cb_register_base_programs(kernel);
    
    if (cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee) != 0) return -20;
    if (cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_tee_exec) != 0) return -20;
    if (cb_kernel_register_executor(kernel, &tee_wrapper_ops, &mock_peer) != 0) return -21;

    /* Interleaved tasks */
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -22;
    r1 = cb_kernel_run(kernel); 
    
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -23;
    r2 = cb_kernel_run(kernel); 
    
    if (cb_tee_head != NULL) { cb_kernel_destroy(kernel); return 200; }
    
    if (cb_kernel_run(kernel) != 0) return 201; 
    if (cb_kernel_run(kernel) != 0) return 202; 

    /* Phase 3: Creation failures preserving active global */
    if (cb_kernel_boot(kernel, "mock_tee") != 0) return -24;
    cb_kernel_run(kernel); /* active yielded */
    
    target_alloc_fail = alloc_count + 1;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 300;
    if (cb_tee_head != NULL) return 301;
    
    target_alloc_fail = alloc_count + 2;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 302;
    if (cb_tee_head != NULL) return 303;
    
    target_alloc_fail = 0;
    
    target_context_fail = context_count + 1;
    if (cb_kernel_boot(kernel, "mock_peer") == 0) return 304;
    if (cb_tee_head != NULL) return 305;
    target_context_fail = 0;
    
    if (cb_kernel_run(kernel) != 0) return 306;
    
    /* Phase 4: Exec tests */
    if (cb_kernel_boot(kernel, "mock_tee_exec") != 0) return -30;
    if (cb_kernel_run(kernel) != 0) return 400;

    cb_kernel_destroy(kernel);
    if (live_allocs != 0) return 500;
    
    return 0;
}
