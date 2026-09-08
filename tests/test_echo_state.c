#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_netbsdecho_main(int argc, char **argv);

static const struct cb_api_v1 *runtime_api;
static int fail_stdout_write;
static char captured_out[128];
static char captured_err[128];
static size_t captured_out_length;
static size_t captured_err_length;

static void failure(const char *message)
{
    fprintf(stderr, "FAIL: echo state %s\n", message);
    exit(1);
}

static cb_ssize_t injected_write(int fd, const void *data, size_t length)
{
    if (fd == 1 && fail_stdout_write) {
        runtime_api->set_errno(CB_EPIPE);
        return -1;
    }
    if (fd < 1 || fd > 2)
        failure("injected write fd");
    if (fd == 1) {
        if (captured_out_length + length > sizeof(captured_out))
            failure("stdout capture bounds");
        memcpy(captured_out + captured_out_length, data, length);
        captured_out_length += length;
    } else {
        if (captured_err_length + length > sizeof(captured_err))
            failure("stderr capture bounds");
        memcpy(captured_err + captured_err_length, data, length);
        captured_err_length += length;
    }
    return (cb_ssize_t)length;
}

/* Both tasks below call the pinned upstream cb_netbsdecho_main directly
   (not through exec-by-name) with a private copy of the API whose write()
   is replaced, exactly mirroring test_stdio_state.c's injection technique.
   This is the only way to force echo's own "if (ferror(stdout) != 0)
   err(1, ...)" path deterministically, since the fault has to happen on
   the task's very first stdout write. */
static int failing_main(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    (void)envp;
    runtime_api = api;
    copy.write = injected_write;
    return cb_libc_start(&copy, argc, argv, cb_netbsdecho_main);
}
static const struct cb_program_v1 failing_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "netbsdechofail", 0,
    64 * 1024, failing_main
};

static int independent_main(const struct cb_api_v1 *api, int argc,
                            char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    (void)envp;
    runtime_api = api;
    copy.write = injected_write;
    return cb_libc_start(&copy, argc, argv, cb_netbsdecho_main);
}
static const struct cb_program_v1 independent_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "netbsdechoindependent", 0,
    64 * 1024, independent_main
};

static int run(const char *command, const struct cb_program_v1 *program)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    int status;
    if (kernel == NULL) failure("kernel create");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, program) != 0 ||
        cb_kernel_boot(kernel, command) != 0)
        failure("checked registration/boot");
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    return status;
}

/* Proves the write-error/task-isolation row from ECHO-01's prepared test
   matrix, which required STDOUT-01's fault-injection surface to exist:
   a forced stdout write failure makes the pinned upstream echo main hit
   its own "if (ferror(stdout) != 0) err(1, "write error");" path with
   exact status 1 and exact err(3)-formatted stderr, and a second,
   independent task's netbsdecho invocation afterwards succeeds normally
   with completely ordinary output -- proving the error state is
   genuinely per-task, not a shared/global flag. */
void cb_test_echo_state(void)
{
    static const char expected_err[] =
        "netbsdechofail: write error: broken pipe\n";
    static const char expected_out[] = "hello world\n";
    int status;

    fail_stdout_write = 1;
    captured_out_length = 0;
    captured_err_length = 0;
    memset(captured_out, 0, sizeof(captured_out));
    memset(captured_err, 0, sizeof(captured_err));
    status = run("netbsdechofail hello world", &failing_program);
    if (status != 1)
        failure("write failure exit status");
    if (captured_out_length != 0)
        failure("write failure produced stdout output");
    if (captured_err_length != strlen(expected_err) ||
        memcmp(captured_err, expected_err, captured_err_length) != 0)
        failure("write failure exact stderr");

    fail_stdout_write = 0;
    captured_out_length = 0;
    captured_err_length = 0;
    memset(captured_out, 0, sizeof(captured_out));
    memset(captured_err, 0, sizeof(captured_err));
    status = run("netbsdechoindependent hello world", &independent_program);
    if (status != 0)
        failure("independent task exit status");
    if (captured_out_length != strlen(expected_out) ||
        memcmp(captured_out, expected_out, captured_out_length) != 0)
        failure("independent task exact stdout");
    if (captured_err_length != 0)
        failure("independent task unexpected stderr");
}
