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

static int noop_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 0;
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

/* Calls the pinned upstream cb_netbsdecho_main directly (not through
   exec-by-name) with a private copy of the API whose write() is
   replaced, exactly mirroring test_stdio_state.c's injection technique.
   Both the fault and the ordinary case below spawn this same program;
   fail_stdout_write (set by the parent immediately before each spawn,
   and never touched once a child is running) decides which behavior
   injected_write actually exhibits for that child. */
static int fault_main(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    (void)envp;
    runtime_api = api;
    copy.write = injected_write;
    return cb_libc_start(&copy, argc, argv, cb_netbsdecho_main);
}
static const struct cb_program_v1 fault_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "netbsdechofault", 0,
    64 * 1024, fault_main
};

/* Proves the write-error/task-isolation row from ECHO-01's prepared test
   matrix, which required STDOUT-01's fault-injection surface to exist.
   Spawns the same registered "netbsdechofault" program twice from a
   single parent task within one kernel, waiting on each in turn (never
   two children of this kernel running at once): the first spawn, with
   the fault armed, makes the pinned upstream echo main hit its own
   "if (ferror(stdout) != 0) err(1, "write error");" path with exact
   status 1 and exact err(3)-formatted stderr; the second, unarmed spawn
   afterwards succeeds normally with completely ordinary output -- both
   through the identical program, proving the error state left behind by
   the first child does not leak into the second, sibling task. */
static int parent_main(const struct cb_api_v1 *api, int argc,
                       char *const argv[], char *const envp[])
{
    static const char expected_err[] =
        "netbsdechofault: write error: broken pipe\n";
    static const char expected_out[] = "hello world\n";
    char *child_argv[] = {(char *)"netbsdechofault", (char *)"hello",
                          (char *)"world", NULL};
    cb_pid_t child;
    int status;
    (void)argc;
    (void)argv;

    fail_stdout_write = 1;
    captured_out_length = 0;
    captured_err_length = 0;
    if (api->spawn("netbsdechofault", child_argv, envp, NULL, 0, &child) < 0)
        return 90;
    if (api->waitpid(child, &status) != child)
        return 91;
    /* The fault child's cb_libc_start(&copy, ...) call left cb_libc's
       internal binding pointed at "copy", a struct local to that now-
       terminated task's own stack frame -- and its failure path exits
       through err()/cb_libc_exit(), which never returns control to that
       task's wrapper, so nothing in that child ever gets a chance to
       restore it. Rebind explicitly to the kernel's own real api before
       this parent (or anything scheduled after it) can call into
       cb_libc again, mirroring test_stdio_state.c's own injected_main
       rebind-after-mock idiom. */
    if (cb_libc_start(api, 0, NULL, noop_main) != 0)
        return 100;
    if (status != 1)
        return 92;
    if (captured_out_length != 0)
        return 93;
    if (captured_err_length != strlen(expected_err) ||
        memcmp(captured_err, expected_err, captured_err_length) != 0)
        return 94;

    fail_stdout_write = 0;
    captured_out_length = 0;
    captured_err_length = 0;
    if (api->spawn("netbsdechofault", child_argv, envp, NULL, 0, &child) < 0)
        return 95;
    if (api->waitpid(child, &status) != child)
        return 96;
    /* This child returns normally, but rebind here too, for the same
       reason and the same safety margin -- not because this specific
       call path needs it. */
    if (cb_libc_start(api, 0, NULL, noop_main) != 0)
        return 101;
    if (status != 0)
        return 97;
    if (captured_out_length != strlen(expected_out) ||
        memcmp(captured_out, expected_out, captured_out_length) != 0)
        return 98;
    if (captured_err_length != 0)
        return 99;
    return 0;
}
static const struct cb_program_v1 parent_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "echoparent", 0,
    64 * 1024, parent_main
};

void cb_test_echo_state(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    int status;
    if (kernel == NULL) failure("kernel create");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &fault_program) != 0 ||
        cb_kernel_register(kernel, &parent_program) != 0 ||
        cb_kernel_boot(kernel, "echoparent") != 0)
        failure("checked registration/boot");
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    if (status != 0) {
        fprintf(stderr, "echoparent returned %d\n", status);
        failure("write-failure/task-isolation probe");
    }
}
