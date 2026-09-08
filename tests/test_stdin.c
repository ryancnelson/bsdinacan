#include "internal.h"
#include "cannedbsd/libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_stdin_call(const struct cb_api_v1 *, const char *);
extern const struct cb_program_v1 cb_stdin_probe_program, cb_stdin_compat_program;
static const struct cb_api_v1 *real_api;
static unsigned calls;
static int plan;
static void fail(const char *message)
{
    fprintf(stderr, "FAIL: stdin %s\n", message); exit(1);
}
static struct cb_input_state_v1 *disturb_state(void)
{
    struct cb_input_state_v1 *state = real_api->input_state_location();
    real_api->set_errno(CB_EINVAL);
    return state;
}
static cb_ssize_t read_input(int fd, void *buffer, size_t size)
{
    static const unsigned char bytes[] = {0,255,'A'};
    unsigned call = calls++;
    if (fd != 0 || size != 1) fail("read arguments");
    if (plan == 1) {
        if (call == 0) { real_api->set_errno(CB_EIO); return -1; }
        if (call == 1) { *(unsigned char *)buffer = 'R'; real_api->set_errno(CB_EINVAL); return 1; }
        real_api->set_errno(CB_EINVAL); return 0;
    }
    real_api->set_errno(CB_EINVAL);
    if (plan == 2 || call == sizeof(bytes)) return 0;
    if (call > sizeof(bytes)) fail("read after EOF");
    *(unsigned char *)buffer = bytes[call]; return 1;
}
static int noop(int argc, char **argv)
{ (void)argc; (void)argv; return 0; }
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    int result;
    (void)envp;
    if (argc != 2) return 60;
    real_api = api;
    copy.read = read_input;
    copy.input_state_location = disturb_state;
    result = cb_stdin_call(&copy, argv[1]);
    /* Restore even after a rejected assertion; no stack-bound API escapes. */
    (void)cb_libc_start(api, 0, NULL, noop);
    return result;
}
static const struct cb_program_v1 injected = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdininject", 0,
    64 * 1024, entry
};
static void run(const struct cb_program_v1 *program, const char *command)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    int status;
    if (kernel == NULL) fail("create");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, program) != 0 || cb_kernel_boot(kernel, command) != 0)
        fail("registration/boot");
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    if (status != 0) {
        fprintf(stderr, "%s status %d\n", command, status); fail("ordinary probe");
    }
}
void cb_test_stdin(void)
{
    static const struct { const char *command; int mode; unsigned count; } cases[] = {
        {"stdininject binary",0,4}, {"stdininject recover",1,3},
        {"stdininject eof",2,1}, {"stdininject invalid",0,0},
        {"stdininject output-error",0,0}
    };
    size_t i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        calls = 0; plan = cases[i].mode;
        run(&injected, cases[i].command);
        if (calls != cases[i].count) fail("exact read count");
    }
    run(&cb_stdin_probe_program, "stdinprobe");
    run(&cb_stdin_compat_program, "stdincompat");
    puts("stdin tests passed");
}
