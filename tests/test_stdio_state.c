#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_stdio_state_probe_main(int argc, char **argv);
extern const struct cb_program_v1 cb_stdio_oldtable_program;

static const struct cb_api_v1 *runtime_api;
static int write_plan;
static unsigned write_calls;
static char output[2][64];
static size_t output_length[2];
static int peer_observed;

static void failure(const char *message)
{
    fprintf(stderr, "FAIL: stdio state %s\n", message);
    exit(1);
}
static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }

static cb_ssize_t injected_write(int fd, const void *data, size_t length)
{
    unsigned call = write_calls++;
    if ((write_plan == 2 && call == 1) || (write_plan == 3 && call == 0)) {
        runtime_api->set_errno(CB_EPIPE);
        return -1;
    }
    if (write_plan == 4 && call == 0)
        return 0;
    if ((write_plan == 1 || (write_plan == 2 && call == 0)) && length > 2)
        length = 2;
    if (fd < 1 || fd > 2 || output_length[fd - 1] + length > sizeof(output[0]))
        failure("injected write bounds");
    memcpy(output[fd - 1] + output_length[fd - 1], data, length);
    output_length[fd - 1] += length;
    /* Successful host adapters can disturb task errno; the veneer must
       retain the incoming value independently of callback side effects. */
    runtime_api->set_errno(CB_EIO);
    return (cb_ssize_t)length;
}

static int injected_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    int result;
    (void)envp;
    runtime_api = api;
    copy.write = injected_write;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_state_probe_main);
    if (cb_libc_start(api, 0, NULL, noop) != 0)
        return 90;
    return result;
}
static const struct cb_program_v1 injected_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdioinject", 0,
    64 * 1024, injected_main
};

static int call_probe(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"stdio", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_stdio_state_probe_main);
}

static int lifecycle_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    cb_pid_t child;
    int status;
    char *peer[] = {(char *)"stdiopeer", NULL};
    char *replacement[] = {(char *)"stdioafterexec", NULL};
    char *missing[] = {(char *)"missing", NULL};
    (void)argc;
    if (strcmp(argv[0], "stdioafterexec") == 0)
        return call_probe(api, "clean");
    if (call_probe(api, "clean") != 0) return 40;
    if (strcmp(argv[0], "stdiopeer") == 0) {
        if (call_probe(api, "stderr_dirty") != 0) return 41;
        peer_observed = 1;
        api->yield();
        return call_probe(api, "stderr_persist");
    }
    if (call_probe(api, "rebind") != 0) return 42;
    if (api->exec("stdio_missing", missing, envp) >= 0 ||
        api->get_errno() != CB_ENOENT || call_probe(api, "rebind_persist") != 0)
        return 43;
    if (api->spawn("stdiolifecycle", peer, envp, NULL, 0, &child) < 0)
        return 44;
    api->yield();
    if (!peer_observed || call_probe(api, "rebind_persist") != 0) return 45;
    if (api->waitpid(child, &status) != child || status != 0) return 46;
    if (call_probe(api, "rebind_persist") != 0) return 47;
    api->exec("stdiolifecycle", replacement, envp);
    return 48; /* success must execute the clean-state replacement */
}
static const struct cb_program_v1 lifecycle_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdiolifecycle", 0,
    64 * 1024, lifecycle_main
};

static void run(const char *command, const struct cb_program_v1 *program)
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
    if (status != 0) {
        fprintf(stderr, "command %s returned %d\n", command, status);
        failure("probe exit");
    }
}

void cb_test_stdio_state(void)
{
    static const struct {
        const char *command;
        int plan;
        const char *out;
        const char *err;
        unsigned calls;
    } cases[] = {
        {"stdioinject putchar_ok", 0, "A", "", 1},
        {"stdioinject byte", 0, "\xff", "", 1},
        {"stdioinject write_retry", 1, "hello\n", "", 4},
        {"stdioinject sticky_error", 2, "abOK", "", 3},
        {"stdioinject sticky_error", 3, "OK", "", 2},
        {"stdioinject zero", 4, "", "", 1},
        {"stdioinject printf_fail", 3, "", "", 1},
        {"stdioinject fprintf_fail", 3, "", "", 1},
        {"stdioinject raw_fail", 3, "", "", 1},
        {"stdioinject format", 0, "", "", 0},
        {"stdioinject fflush_invalid", 0, "", "", 0}
    };
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        write_plan = cases[index].plan;
        write_calls = 0;
        memset(output_length, 0, sizeof(output_length));
        memset(output, 0, sizeof(output));
        run(cases[index].command, &injected_program);
        if (write_calls != cases[index].calls ||
            output_length[0] != strlen(cases[index].out) ||
            output_length[1] != strlen(cases[index].err) ||
            memcmp(output[0], cases[index].out, output_length[0]) != 0 ||
            memcmp(output[1], cases[index].err, output_length[1]) != 0)
            failure("exact injected writes/streams");
    }
    run("stdiooldtable", &cb_stdio_oldtable_program);
    peer_observed = 0;
    run("stdiolifecycle", &lifecycle_program);
}
