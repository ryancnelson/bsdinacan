#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_fwrite_probe_main(int argc, char **argv);

static const struct cb_api_v1 *runtime_api;
static int write_plan;
static unsigned write_calls;
static char output[2][64];
static size_t output_length[2];
static struct cb_stdio_state_v1 global_state;
static int missing_stdio = 0;

static void failure(const char *message)
{
    fprintf(stderr, "FAIL: fwrite %s\n", message);
    exit(1);
}

static cb_ssize_t injected_write(int fd, const void *data, size_t length)
{
    unsigned call = write_calls++;
    if (fd != 1 && fd != 2) return -1;
    int idx = fd - 1;
    
    runtime_api->set_errno(CB_EBADF); /* disturb errno to prove preservation */

    if (write_plan == 0) { /* Full write */
        if (output_length[idx] + length <= sizeof(output[0])) {
            memcpy(output[idx] + output_length[idx], data, length);
            output_length[idx] += length;
            return length;
        }
        return -1;
    }
    if (write_plan == 1) { /* multiple positive short writes */
        size_t chunk = (length > 2) ? 2 : length;
        if (output_length[idx] + chunk <= sizeof(output[0])) {
            memcpy(output[idx] + output_length[idx], data, chunk);
            output_length[idx] += chunk;
            return chunk;
        }
        return -1;
    }
    if (write_plan == 2) { /* zero before progress */
        return 0;
    }
    if (write_plan == 3) { /* partial then zero */
        if (call == 0) {
            size_t chunk = 6;
            if (length < chunk) chunk = length;
            memcpy(output[idx] + output_length[idx], data, chunk);
            output_length[idx] += chunk;
            return chunk;
        }
        return 0;
    }
    if (write_plan == 4) { /* partial then negative */
        if (call == 0) {
            size_t chunk = 6; /* 6 bytes emitted */
            if (length < chunk) chunk = length;
            memcpy(output[idx] + output_length[idx], data, chunk);
            output_length[idx] += chunk;
            return chunk;
        }
        runtime_api->set_errno(CB_EPIPE);
        return -1;
    }
    if (write_plan == 5) { /* oversized positive return */
        return (cb_ssize_t)((uint64_t)length + 1ULL);
    }
    
    return -1;
}

static struct cb_stdio_state_v1 *injected_stdio(void)
{
    if (missing_stdio) return NULL;
    return &global_state;
}

static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }

static int injected_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy = *api;
    int result;
    (void)envp;
    runtime_api = api;
    copy.write = injected_write;
    copy.stdio_state_location = injected_stdio;
    
    /* Global state init */
    memset(&global_state, 0, sizeof(global_state));
    global_state.abi_version = CB_ABI_VERSION_V1;
    global_state.struct_size = sizeof(global_state);
    

    if (strcmp(argv[1], "state_version") == 0) {
        global_state.abi_version = 2;
    }
    if (strcmp(argv[1], "state_short") == 0) {
        global_state.struct_size = 0;
    }
    if (strcmp(argv[1], "state_null") == 0) {
        missing_stdio = 1;
    }
    if (strcmp(argv[1], "state_missing_cb") == 0) {
        copy.stdio_state_location = NULL;
    }
    if (strcmp(argv[1], "state_recover") == 0) {
        global_state.struct_size = 0;
        char *fake_argv[] = { argv[0], "state_old", NULL }; if (cb_libc_start(&copy, argc, fake_argv, cb_fwrite_probe_main) != 0) return 90;
        global_state.struct_size = sizeof(global_state);
        if (global_state.stdout_error) return 91;
        result = cb_libc_start(&copy, argc, argv, cb_fwrite_probe_main);
        if (cb_libc_start(api, 0, NULL, noop) != 0) return 92;
        return result;
    }

    if (strcmp(argv[1], "sticky") == 0) {
        global_state.stdout_error = 1; /* sticky error set */
    }
    if (strcmp(argv[1], "stderr_isolate") == 0) {
        global_state.stdout_error = 0;
        global_state.stderr_error = 0;
    }

    result = cb_libc_start(&copy, argc, argv, cb_fwrite_probe_main);
    if (cb_libc_start(api, 0, NULL, noop) != 0) return 90;

    /* Verify flags aren't mutated for independent rejections */
    if (strncmp(argv[1], "state_", 6) == 0) {
        if (global_state.stdout_error) return 91; /* Unchanged flags */
    }

    return result;
}

static const struct cb_program_v1 injected_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "fwriteinject", 0,
    64 * 1024, injected_main
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
    status = cb_kernel_run(kernel); if (status != 0) { fprintf(stderr, "command %s returned %d\n", command, status); failure("fwrite probe exit"); }
    cb_kernel_destroy(kernel);
    if (status != 0) {
        fprintf(stderr, "command %s returned %d\n", command, status);
        failure("probe exit");
    }
}

void cb_test_fwrite(void)
{
    static const struct {
        const char *command;
        int plan;
        const char *out;
        const char *err;
        unsigned calls;
    } cases[] = {
        {"fwriteinject full", 0, "abcd", "", 1},
        {"fwriteinject short", 1, "abcdef", "", 3},
        {"fwriteinject zero_progress", 2, "", "", 1},
        {"fwriteinject partial_zero", 3, "abcdef", "", 2},
        {"fwriteinject partial_negative", 4, "abcdef", "", 2},
        {"fwriteinject oversized", 5, "", "", 1},
        {"fwriteinject sticky", 0, "abcd", "", 1},
        {"fwriteinject stderr_write", 0, "", "err", 1},
        {"fwriteinject stderr_isolate", 4, "", "abcdef", 2},
        {"fwriteinject zero_requests", 0, "", "", 0},
        {"fwriteinject invalid_streams", 0, "", "", 0},
        {"fwriteinject null_buffer", 0, "", "", 0},
        {"fwriteinject null_buffer_zero", 0, "", "", 0},
        {"fwriteinject overflow", 0, "", "", 0},
        {"fwriteinject state_version", 0, "", "", 0},
        {"fwriteinject state_short", 0, "", "", 0},
        {"fwriteinject state_null", 0, "", "", 0},
        {"fwriteinject state_missing_cb", 0, "", "", 0},
        {"fwriteinject state_recover", 0, "a", "", 1}
    };
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        write_plan = cases[index].plan;
        write_calls = 0;
        missing_stdio = 0;
        memset(output_length, 0, sizeof(output_length));
        memset(output, 0, sizeof(output));
        run(cases[index].command, &injected_program);
        if (write_calls != cases[index].calls ||
            output_length[0] != strlen(cases[index].out) ||
            output_length[1] != strlen(cases[index].err) ||
            memcmp(output[0], cases[index].out, output_length[0]) != 0 ||
            memcmp(output[1], cases[index].err, output_length[1]) != 0) {
            fprintf(stderr, "Got output %zu '%s', write_calls %u\n", output_length[0], output[0], write_calls);
            fprintf(stderr, "\nFAIL AT INDEX %zu: expected %s, got %s (calls %u)\n", index, cases[index].out, output[0], write_calls); failure("exact injected writes/streams");}
    }
}
