#include "cannedbsd/libc.h"

extern int cb_stdio_oldtable_main(int argc, char **argv);
extern int cb_stdio_state_probe_main(int argc, char **argv);

static struct cb_stdio_state_v1 bad_state;
static unsigned unexpected_writes;
static struct cb_stdio_state_v1 *state_location(void) { return &bad_state; }
static struct cb_stdio_state_v1 *null_location(void) { return NULL; }
static cb_ssize_t reject_write(int fd, const void *data, size_t length)
{
    (void)fd; (void)data; (void)length;
    ++unexpected_writes;
    return -1;
}
static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }

static int variants_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result = 0;
    unsigned variant;
    char *seed[] = {(char *)"stdio", (char *)"rebind", NULL};
    char *check[] = {(char *)"stdio", (char *)"rebind_persist", NULL};
    (void)envp;
    unexpected_writes = 0;
    for (variant = 0; variant < 5; ++variant) {
        copy = *api;
        copy.write = reject_write;
        bad_state.abi_version = CB_ABI_VERSION_V1;
        bad_state.struct_size = sizeof(bad_state);
        bad_state.stdout_error = 73;
        bad_state.stderr_error = 91;
        if (variant == 0)
            copy.struct_size = (uint32_t)offsetof(struct cb_api_v1, stdio_state_location);
        else if (variant == 1)
            copy.stdio_state_location = NULL;
        else if (variant == 2)
            copy.stdio_state_location = null_location;
        else {
            copy.stdio_state_location = state_location;
            if (variant == 3) bad_state.struct_size = offsetof(struct cb_stdio_state_v1, stderr_error);
            else bad_state.abi_version = CB_ABI_VERSION_V1 + 1;
        }
        result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
        /* Restore on every exit, while the copied table is still alive. */
        if (cb_libc_start(api, 0, NULL, noop) != 0) return 40;
        if (result != 0 || unexpected_writes != 0 ||
            bad_state.stdout_error != 73 || bad_state.stderr_error != 91)
            return 41 + (int)variant;
        /* Old output still works through each degraded table and must not
           write either indicator in malformed state. */
        if (cb_libc_start(&copy, 0, NULL, noop) != 0) return 46;
        result = cb_libc_puts("legacy-failure");
        if (cb_libc_start(api, 0, NULL, noop) != 0) return 47;
        if (result >= 0 || unexpected_writes != 1 ||
            bad_state.stdout_error != 73 || bad_state.stderr_error != 91) return 48;
        unexpected_writes = 0;
    }
    /* Real EBADF in the guest too: never depend on a host-only mock. */
    result = cb_libc_start(api, 2, seed, cb_stdio_state_probe_main);
    if (result != 0) return 50;
    copy = *api;
    copy.stdio_state_location = NULL;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (cb_libc_start(api, 0, NULL, noop) != 0) return 51;
    if (result != 0) return 52;
    return cb_libc_start(api, 2, check, cb_stdio_state_probe_main);
}
const struct cb_program_v1 cb_stdio_oldtable_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdiooldtable", 0,
    64 * 1024, variants_main
};
