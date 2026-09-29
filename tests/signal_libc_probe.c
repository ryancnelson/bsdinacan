#include "internal.h"
#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_signal_libc_main(int, char **);
static struct cb_kernel *kernel;
static unsigned reached_after_default;

static int call(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"signal-libc", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_signal_libc_main);
}

static int child(const struct cb_api_v1 *api, int argc,
                 char *const argv[], char *const envp[])
{
    char *next[] = {(char *)"signal-child", (char *)"after-exec", NULL};
    struct cb_task *self = kernel->current;
    int result;
    if (argc != 2) return 30;
    if (strcmp(argv[1], "default") == 0) {
        if (call(api, "default") != 0 ||
            cb_kernel_request_interrupt(kernel, api->getpid()) != 0) return 31;
        api->yield();
        ++reached_after_default;
        return 32;
    }
    if (strcmp(argv[1], "ignored") == 0) {
        if (call(api, "ignore") != 0 ||
            cb_kernel_request_interrupt(kernel, api->getpid()) != 0) return 33;
        api->yield();
        if (call(api, "inherited") != 0 || self->interrupt_pending) return 34;
        if (api->exec("absent-signal-program", next, envp) != -1 ||
            api->get_errno() != CB_ENOENT || call(api, "inherited") != 0)
            return 35;
        api->exec("signal-child", next, envp);
        return 36;
    }
    if (strcmp(argv[1], "after-exec") == 0) {
        result = call(api, "inherited");
        if (result != 0 || self->interrupt_pending) return 37;
        return 0;
    }
    if (strcmp(argv[1], "unavailable") == 0)
        return call(api, "unavailable");
    if (strcmp(argv[1], "inherited") == 0)
        return call(api, "inherited");
    return 38;
}

static int entry(const struct cb_api_v1 *api, int argc,
                 char *const argv[], char *const envp[])
{
    struct cb_task *self = kernel->current;
    struct cb_api_v1 absent = *api;
    const size_t sizes[] = {offsetof(struct cb_api_v1, poll),
                           offsetof(struct cb_api_v1, set_interrupt),
                           offsetof(struct cb_api_v1, set_interrupt) +
                               sizeof(api->set_interrupt) - 1};
    char *default_args[] = {(char *)"signal-child", (char *)"default", NULL};
    char *ignored_args[] = {(char *)"signal-child", (char *)"ignored", NULL};
    char *inherited_args[] = {(char *)"signal-child", (char *)"inherited", NULL};
    char *unsupported_args[] = {(char *)"signal-unsupported", (char *)"unavailable", NULL};
    cb_pid_t default_pid, ignored_pid, inherited_pid;
    size_t i;
    int status, previous = 77;
    (void)argc; (void)argv;
    /* The frozen pre-SIG-02 table includes rename but no interrupt setter.
       Allocate only that prefix, and verify the real filesystem mutation. */
    {
        size_t old_size = offsetof(struct cb_api_v1, rename) + sizeof(api->rename);
        struct cb_api_v1 *old_api = kernel->host->allocate(old_size);
        int fd, result;
        char content = 0;
        if (old_api == NULL) return 61;
        memcpy(old_api, api, old_size);
        old_api->struct_size = (uint32_t)old_size;
        fd = api->open("/signal-rename-source", CB_O_WRONLY | CB_O_CREAT, 0600);
        if (fd < 0 || api->write(fd, "R", 1) != 1 || api->close(fd) != 0) {
            kernel->host->release(old_api);
            return 62;
        }
        result = call(old_api, "rename-oldtable");
        kernel->host->release(old_api);
        if (result != 0) return result;
        fd = api->open("/signal-rename-target", CB_O_RDONLY, 0);
        if (fd < 0 || api->read(fd, &content, 1) != 1 || content != 'R' ||
            api->close(fd) != 0 ||
            api->open("/signal-rename-source", CB_O_RDONLY, 0) != -1 ||
            api->get_errno() != CB_ENOENT) return 63;
    }
    if (call(api, "basic") != 0) return 40;
    api->set_errno(CB_EPIPE);
    if (api->set_interrupt(99, &previous) != -1 ||
        api->get_errno() != CB_EINVAL || previous != 77 ||
        api->set_interrupt(CB_INTERRUPT_IGNORE, NULL) != -1 ||
        api->get_errno() != CB_EINVAL) return 41;
    /* A real pending request must survive every rejecting veneer path. */
    if (cb_kernel_request_interrupt(kernel, api->getpid()) != 0 ||
        call(api, "reject") != 0 || !self->interrupt_pending ||
        self->interrupt_disposition != CB_INTERRUPT_DEFAULT) return 42;
    for (i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        struct cb_api_v1 *short_api = kernel->host->allocate(sizes[i]);
        int result;
        if (short_api == NULL) return 43;
        memcpy(short_api, api, sizes[i]);
        short_api->struct_size = (uint32_t)sizes[i];
        result = call(short_api, "unavailable");
        kernel->host->release(short_api);
        if (result != 0 || !self->interrupt_pending ||
            self->interrupt_disposition != CB_INTERRUPT_DEFAULT) return 44;
    }
    absent.set_interrupt = NULL;
    if (call(&absent, "unavailable") != 0 || !self->interrupt_pending ||
        call(api, "ignore") != 0 || self->interrupt_pending) return 45;
    /* IGN clears a queued request. A fresh real request while ignored is
       discarded too; yielding proves neither will later terminate us. */
    if (cb_kernel_request_interrupt(kernel, api->getpid()) != 0) return 46;
    api->yield();
    if (call(api, "inherited") != 0 || self->interrupt_pending) return 47;
    if (api->spawn("signal-child", inherited_args, envp, NULL, 0,
                   &inherited_pid) != 0 || call(api, "restore") != 0) return 48;
    if (api->waitpid(inherited_pid, &status) != inherited_pid || status != 0 ||
        call(api, "default") != 0) return 49;
    /* Independent peer dispositions, real default termination and ignore
       surviving both failed and successful exec. */
    if (api->spawn("signal-child", default_args, envp, NULL, 0, &default_pid) != 0 ||
        api->spawn("signal-child", ignored_args, envp, NULL, 0, &ignored_pid) != 0)
        return 50;
    if (api->waitpid(default_pid, &status) != default_pid || status != 130 ||
        reached_after_default != 0) return 51;
    if (api->waitpid(ignored_pid, &status) != ignored_pid || status != 0 ||
        call(api, "default") != 0) return 52;
    if (api->spawn("signal-unsupported", unsupported_args, envp, NULL, 0,
                   &inherited_pid) != 0 ||
        api->waitpid(inherited_pid, &status) != inherited_pid || status != 0)
        return 53;
    return 0;
}

static const struct cb_program_v1 program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "signal-libc", 0,
    64 * 1024, entry
};
static const struct cb_program_v1 child_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "signal-child", 0,
    64 * 1024, child
};
int cb_signal_libc_probe(const struct cb_host_ops_v1 *host)
{
    int status;
    struct cb_executor_ops unsupported_ops = *cb_native_executor();
    const struct cb_program_v1 unsupported_program = {
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "signal-unsupported", 0,
        64 * 1024, child
    };
    unsupported_ops.capabilities = 0;
    reached_after_default = 0;
    kernel = cb_kernel_create(host);
    if (kernel != NULL) cb_register_base_programs(kernel);
    if (kernel == NULL || cb_kernel_register(kernel, &program) != 0 ||
        cb_kernel_register(kernel, &child_program) != 0 ||
        cb_kernel_register_executor(kernel, &unsupported_ops, &unsupported_program) != 0 ||
        cb_kernel_boot(kernel, "signal-libc") != 0) {
        if (kernel != NULL) cb_kernel_destroy(kernel);
        kernel = NULL;
        return 60;
    }
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    kernel = NULL;
    return status;
}
