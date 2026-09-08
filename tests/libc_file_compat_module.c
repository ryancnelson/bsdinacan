#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_file_call(const struct cb_api_v1 *, const char *);
extern int cb_file_prepare(const struct cb_api_v1 *);
extern int cb_stdin_call(const struct cb_api_v1 *, const char *);
static const struct cb_api_v1 *real_api;
static struct cb_input_state_v1 *supplied;
static unsigned opens, reads, closes, allocations, releases;
static int no_open(const char *path, int flags, uint32_t mode)
{ (void)path; (void)flags; (void)mode; ++opens; return -1; }
static cb_ssize_t empty_read(int fd, void *buffer, size_t count)
{ (void)fd; (void)buffer; (void)count; ++reads; return 0; }
static int no_close(int fd) { (void)fd; ++closes; return -1; }
static void *no_allocate(size_t size) { (void)size; ++allocations; return NULL; }
static void no_release(void *pointer) { (void)pointer; ++releases; }
static struct cb_input_state_v1 *get_input(void)
{ real_api->set_errno(CB_EIO); return supplied; }
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy, *bound;
    struct cb_input_state_v1 bad, original;
    void *short_api = NULL, *short_state = NULL;
    int variant, result = 0;
    (void)argc; (void)argv; (void)envp;
    real_api = api;
    if (cb_file_prepare(api) != 0 || cb_file_call(api, "open-one") != 0) return 60;
    memcpy(&original, api->input_state_location(), sizeof(original));
    short_api = api->allocate(offsetof(struct cb_api_v1, input_state_location));
    if (short_api == NULL) { result = 61; goto cleanup; }
    for (variant = 0; variant < 7; ++variant) {
        copy = *api;
        copy.open = no_open; copy.read = empty_read; copy.close = no_close;
        copy.allocate = no_allocate; copy.release = no_release;
        copy.input_state_location = get_input;
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
            if (short_state == NULL) { result = 68; break; }
            memcpy(short_state, &bad, bad.struct_size); supplied = short_state;
        }
        opens = reads = closes = allocations = releases = 0;
        result = cb_file_call(bound, "unavailable");
        if (opens || reads || closes || allocations || releases) result = 62;
        /* Exact old stage1 and partial stage2 states still support stdin. */
        if (result == 0 && variant >= 5) {
            result = cb_stdin_call(bound, "eof");
            if (reads != 1 || opens || closes || allocations || releases) result = 63;
        }
        /* Restore on every path before either short or stack table expires. */
        if (cb_file_call(api, "clean-stdin") != 0 ||
            memcmp(&original, api->input_state_location(), sizeof(original)) != 0)
            result = 64;
        api->release(short_state); short_state = NULL;
        if (result != 0) break;
    }
    /* Cleanup operations are part of the mandatory startup prefix. */
    for (variant = 0; result == 0 && variant < 4; ++variant) {
        copy = *api;
        copy.open = no_open;
        if (variant == 0) copy.close = NULL;
        if (variant == 1) copy.allocate = NULL;
        if (variant == 2) copy.release = NULL;
        if (variant == 3) copy.open = NULL;
        opens = 0;
        if (cb_file_call(&copy, "open-one") != 126 || opens != 0) result = 65;
        if (cb_file_call(api, "clean-stdin") != 0 ||
            memcmp(&original, api->input_state_location(), sizeof(original)) != 0)
            result = 66;
    }
    if (result == 0 && (cb_file_call(api, "read-one") != 0 ||
                       cb_file_call(api, "close-one") != 0)) result = 67;
cleanup:
    api->release(short_api); api->release(short_state);
    supplied = NULL;
    return result;
}
const struct cb_program_v1 cb_file_compat_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "filecompat", 0,
    64 * 1024, entry
};
