#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_fread_call(const struct cb_api_v1 *, const char *);
static const struct cb_api_v1 *real_api;
static struct cb_input_state_v1 *supplied;
static unsigned reads, states;
static cb_ssize_t read_legacy(int fd, void *buffer, size_t size)
{
    unsigned call = reads++;
    if (fd != 0 || size != 1) return -1;
    real_api->set_errno(CB_EINVAL);
    if (call == 0) { *(unsigned char *)buffer = 0; return 1; }
    return 0;
}
static struct cb_input_state_v1 *get_state(void)
{ ++states; real_api->set_errno(CB_EIO); return supplied; }
static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    static const unsigned char data[] = {0,255};
    struct cb_api_v1 copy, *bound;
    struct cb_input_state_v1 bad, original;
    unsigned char before[sizeof(bad)];
    void *short_api = NULL, *short_state = NULL;
    int fd, variant, result = 0;
    (void)argc; (void)argv; (void)envp;
    real_api = api;
    fd = api->open("/tmp/fread-input", CB_O_CREAT | CB_O_TRUNC | CB_O_WRONLY, 0600);
    if (fd < 0 || api->write(fd, data, sizeof(data)) != (cb_ssize_t)sizeof(data) ||
        api->close(fd) != 0 || cb_fread_call(api, "open-held") != 0) return 60;
    memcpy(&original, api->input_state_location(), sizeof(original));
    short_api = api->allocate(offsetof(struct cb_api_v1, input_state_location));
    if (short_api == NULL) return 61;
    for (variant = 0; variant < 7; ++variant) {
        copy = *api; copy.read = read_legacy; copy.input_state_location = get_state;
        memset(&bad, 0, sizeof(bad));
        bad.abi_version = CB_ABI_VERSION_V1; bad.struct_size = sizeof(bad);
        supplied = &bad; bound = &copy;
        if (variant == 0) {
            copy.struct_size = offsetof(struct cb_api_v1, input_state_location);
            memcpy(short_api, &copy, copy.struct_size); bound = short_api;
        } else if (variant == 1) copy.input_state_location = NULL;
        else if (variant == 2) supplied = NULL;
        else if (variant == 3) bad.abi_version = 0;
        else {
            bad.struct_size = variant == 4 ? offsetof(struct cb_input_state_v1, stdin_error) :
                variant == 5 ? CB_INPUT_STATE_V1_MIN_SIZE :
                offsetof(struct cb_input_state_v1, input_streams);
            short_state = api->allocate(bad.struct_size);
            if (short_state == NULL) { result = 62; break; }
            memcpy(short_state, &bad, bad.struct_size); supplied = short_state;
        }
        memcpy(before, &bad, sizeof(bad)); reads = states = 0;
        result = cb_fread_call(bound, "zero-quiet");
        if (reads || states) result = 63;
        if (result == 0) result = cb_fread_call(bound, "held-unavailable");
        if (result == 0 && variant < 5) result = cb_fread_call(bound, "unavailable");
        if (reads || (variant < 2 && states) || memcmp(before, &bad, sizeof(bad)) != 0)
            result = 64;
        if (result == 0 && variant >= 5) {
            result = cb_fread_call(bound, "legacy");
            if (reads != 2) result = 65;
        }
        (void)cb_libc_start(api, 0, NULL, noop);
        if (memcmp(&original, api->input_state_location(), sizeof(original)) != 0)
            result = 66;
        api->release(short_state); short_state = NULL;
        if (result != 0) break;
    }
    if (result == 0) result = cb_fread_call(api, "read-held");
    api->release(short_api); api->release(short_state); supplied = NULL;
    return result;
}
const struct cb_program_v1 cb_fread_compat_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "freadcompat", 0, 64 * 1024, entry
};
