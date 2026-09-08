#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_stdin_call(const struct cb_api_v1 *, const char *);
static const struct cb_api_v1 *real_api;
static struct cb_input_state_v1 *supplied;
static struct cb_stdio_state_v1 *output_state;
static unsigned read_calls, state_calls;
static cb_ssize_t reject_read(int fd, void *buffer, size_t size)
{
    (void)fd; (void)buffer; (void)size;
    ++read_calls;
    real_api->set_errno(CB_EIO);
    return -1;
}
static struct cb_input_state_v1 *get_input(void)
{
    ++state_calls;
    real_api->set_errno(CB_EIO);
    return supplied;
}
static struct cb_stdio_state_v1 *get_output(void) { return output_state; }
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    struct cb_api_v1 *bound;
    struct cb_input_state_v1 bad;
    unsigned char before[sizeof(bad)];
    int pipes[2], variant, result = 0;
    void *short_state = NULL, *short_api = NULL;
    (void)argc; (void)argv; (void)envp;
    real_api = api;
    if (api->pipe(pipes) != 0 || api->close(pipes[1]) != 0 ||
        api->dup2(pipes[0], 0) != 0 ||
        (pipes[0] != 0 && api->close(pipes[0]) != 0) ||
        cb_stdin_call(api, "eof") != 0) return 50;
    /* An actual original-sized output object, not a new larger state. */
    output_state = api->allocate(sizeof(*output_state));
    short_state = api->allocate(offsetof(struct cb_input_state_v1, stdin_error));
    short_api = api->allocate(offsetof(struct cb_api_v1, input_state_location));
    if (output_state == NULL || short_state == NULL || short_api == NULL) {
        result = 51; goto cleanup;
    }
    output_state->abi_version = CB_ABI_VERSION_V1;
    output_state->struct_size = sizeof(*output_state);
    output_state->stdout_error = output_state->stderr_error = 0;
    for (variant = 0; variant < 5; ++variant) {
        copy = *api;
        copy.read = reject_read;
        copy.stdio_state_location = get_output;
        copy.input_state_location = get_input;
        bad.abi_version = CB_ABI_VERSION_V1;
        bad.struct_size = sizeof(bad);
        bad.stdin_eof = 17; bad.stdin_error = 23;
        supplied = &bad;
        bound = &copy;
        if (variant == 0) {
            copy.struct_size = offsetof(struct cb_api_v1, input_state_location);
            memcpy(short_api, &copy, copy.struct_size);
            bound = short_api;
        } else if (variant == 1) {
            copy.input_state_location = NULL;
        } else if (variant == 2) {
            supplied = NULL;
        } else if (variant == 3) {
            bad.struct_size = offsetof(struct cb_input_state_v1, stdin_error);
            memcpy(short_state, &bad, bad.struct_size);
            supplied = short_state;
        } else {
            bad.abi_version = 0;
        }
        memcpy(before, &bad, sizeof(bad));
        read_calls = state_calls = 0;
        result = cb_stdin_call(bound, "unavailable");
        if (result == 0 && variant == 0) result = cb_stdin_call(bound, "out-compat");
        /* Restore while stack/short tables still exist, on every result path. */
        if (cb_stdin_call(api, "eof-retained") != 0) result = 52;
        if (read_calls != 0 || state_calls != (variant < 2 ? 0u : 3u) ||
            memcmp(before, &bad, sizeof(bad)) != 0 ||
            (variant == 3 && memcmp(short_state, before, bad.struct_size) != 0) ||
            output_state->stdout_error != 0 || output_state->stderr_error != 0)
            result = 53;
        if (result != 0) break;
    }
cleanup:
    api->release(short_api); api->release(short_state); api->release(output_state);
    supplied = NULL; output_state = NULL;
    return result;
}
const struct cb_program_v1 cb_stdin_compat_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdincompat", 0,
    64 * 1024, entry
};
