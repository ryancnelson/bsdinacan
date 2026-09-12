#include "internal.h"

#include <stdint.h>
#include <string.h>

struct cb_native_program {
    struct cb_program common;
    struct cb_program_v1 descriptor;
};

struct cb_native_execution {
    struct cb_execution common;
    struct cb_host_context *context;
};

static int native_prepare(struct cb_kernel *kernel,
                          const struct cb_executor_ops *executor,
                          const void *source,
                          struct cb_program **program_out);
static struct cb_execution *native_instance_create(
    struct cb_task *task, const struct cb_program *program);
static void native_start_or_resume(struct cb_execution *execution);
static void native_suspend(struct cb_execution *execution);
static void native_request_termination(struct cb_execution *execution);
static void native_instance_destroy(struct cb_execution *execution);
static void native_program_destroy(struct cb_kernel *kernel,
                                   struct cb_program *program);

static const struct cb_executor_ops native_ops = {
    CB_ABI_VERSION_V1,
    sizeof(native_ops),
    native_prepare,
    native_instance_create,
    native_start_or_resume,
    native_suspend,
    native_request_termination,
    native_instance_destroy,
    native_program_destroy,
    CB_EXECUTOR_COOPERATIVE_INTERRUPT
};

static int executor_valid(const struct cb_executor_ops *executor)
{
    return executor != NULL && executor->abi_version == CB_ABI_VERSION_V1 &&
           executor->struct_size >= CB_EXECUTOR_V1_PREFIX_SIZE &&
           executor->prepare != NULL && executor->instance_create != NULL &&
           executor->start_or_resume != NULL && executor->suspend != NULL &&
           executor->request_termination != NULL &&
           executor->instance_destroy != NULL &&
           executor->program_destroy != NULL;
}

int cb_executor_supports_interrupt(const struct cb_executor_ops *executor)
{
    return executor != NULL && executor->abi_version == CB_ABI_VERSION_V1 &&
           executor->struct_size >= offsetof(struct cb_executor_ops, capabilities) +
                                    sizeof(executor->capabilities) &&
           (executor->capabilities & CB_EXECUTOR_COOPERATIVE_INTERRUPT) != 0;
}

const struct cb_executor_ops *cb_native_executor(void)
{
    return &native_ops;
}

int cb_executor_prepare(struct cb_kernel *kernel,
                        const struct cb_executor_ops *executor,
                        const void *source, struct cb_program **program_out)
{
    struct cb_program *program = NULL;
    if (kernel == NULL || source == NULL || program_out == NULL ||
        !executor_valid(executor))
        return -1;
    *program_out = NULL;
    if (executor->prepare(kernel, executor, source, &program) < 0)
        return -1;
    if (program == NULL || program->executor != executor ||
        program->name == NULL || program->name[0] == '\0') {
        if (program != NULL)
            executor->program_destroy(kernel, program);
        return -1;
    }
    *program_out = program;
    return 0;
}

struct cb_execution *cb_executor_instance_create(
    struct cb_task *task, const struct cb_program *program)
{
    struct cb_execution *execution;
    if (task == NULL || program == NULL ||
        !executor_valid(program->executor))
        return NULL;
    execution = program->executor->instance_create(task, program);
    if (execution == NULL || execution->executor != program->executor ||
        execution->task != task || execution->program != program) {
        if (execution != NULL)
            program->executor->instance_destroy(execution);
        return NULL;
    }
    return execution;
}

void cb_executor_start_or_resume(struct cb_execution *execution)
{
    execution->executor->start_or_resume(execution);
}

void cb_executor_suspend(struct cb_execution *execution)
{
    execution->executor->suspend(execution);
}

void cb_executor_request_termination(struct cb_execution *execution)
{
    execution->executor->request_termination(execution);
}

void cb_executor_instance_destroy(struct cb_execution *execution)
{
    if (execution != NULL)
        execution->executor->instance_destroy(execution);
}

void cb_executor_program_destroy(struct cb_kernel *kernel,
                                 struct cb_program *program)
{
    if (program != NULL) {
        program->executor->program_destroy(kernel, program);
        if (kernel->program_count > 0)
            kernel->program_count--;
    }
}

static int native_prepare(struct cb_kernel *kernel,
                          const struct cb_executor_ops *executor,
                          const void *source,
                          struct cb_program **program_out)
{
    const struct cb_program_v1 *descriptor = source;
    struct cb_native_program *program;
    char *name;
    size_t name_size;
    size_t allocation_size;
    if (descriptor->abi_version != CB_ABI_VERSION_V1 ||
        descriptor->struct_size < sizeof(*descriptor) ||
        descriptor->name == NULL || descriptor->name[0] == '\0' ||
        descriptor->flags != 0 || descriptor->start == NULL)
        return -1;
    name_size = strlen(descriptor->name) + 1;
    if (name_size == 0 || name_size > SIZE_MAX - sizeof(*program))
        return -1;
    allocation_size = sizeof(*program) + name_size;
    program = cb_allocate(kernel, allocation_size);
    if (program == NULL)
        return -1;
    name = (char *)(program + 1);
    memcpy(name, descriptor->name, name_size);
    program->common.executor = executor;
    program->common.name = name;
    program->descriptor = *descriptor;
    program->descriptor.name = name;
    *program_out = &program->common;
    return 0;
}

static void native_entry(void *argument)
{
    struct cb_native_execution *native = argument;
    struct cb_task *task = native->common.task;
    const struct cb_native_program *program =
        (const struct cb_native_program *)native->common.program;
    int status;
    task->kernel->current = task;
    task->state = CB_TASK_RUNNING;
    cb_task_deliver_interrupt(task);
    status = program->descriptor.start(&task->kernel->api, task->argc,
                                       task->argv, task->environment);
    task->kernel->api.exit(status);
}

static struct cb_execution *native_instance_create(
    struct cb_task *task, const struct cb_program *common_program)
{
    const struct cb_native_program *program =
        (const struct cb_native_program *)common_program;
    struct cb_native_execution *native =
        cb_allocate(task->kernel, sizeof(*native));
    size_t stack_size = program->descriptor.requested_stack_size;
    if (native == NULL)
        return NULL;
    native->common.executor = common_program->executor;
    native->common.task = task;
    native->common.program = common_program;
    if (stack_size == 0)
        stack_size = 64 * 1024;
    native->context = task->kernel->host->context_create(
        native_entry, native, stack_size);
    if (native->context == NULL) {
        cb_release(task->kernel, native);
        return NULL;
    }
    return &native->common;
}

static void native_start_or_resume(struct cb_execution *execution)
{
    struct cb_native_execution *native =
        (struct cb_native_execution *)execution;
    struct cb_task *task = execution->task;
    task->kernel->host->context_switch(task->kernel->scheduler_context,
                                       native->context);
}

static void native_suspend(struct cb_execution *execution)
{
    struct cb_native_execution *native =
        (struct cb_native_execution *)execution;
    struct cb_task *task = execution->task;
    task->kernel->host->context_switch(native->context,
                                       task->kernel->scheduler_context);
}

static void native_request_termination(struct cb_execution *execution)
{
    struct cb_task *task = execution->task;
    native_suspend(execution);
    task->kernel->host->fatal("terminated native execution resumed");
}

static void native_instance_destroy(struct cb_execution *execution)
{
    struct cb_native_execution *native =
        (struct cb_native_execution *)execution;
    struct cb_kernel *kernel = execution->task->kernel;
    kernel->host->context_destroy(native->context);
    cb_release(kernel, native);
}

static void native_program_destroy(struct cb_kernel *kernel,
                                   struct cb_program *program)
{
    cb_release(kernel, program);
}
