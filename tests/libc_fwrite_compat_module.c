#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_fwrite_probe_main(int, char **);
static const struct cb_api_v1 *real_api;
static struct cb_stdio_state_v1 *supplied;
static unsigned callbacks, writes, mismatch, plan;
static const char *first_buffer;
static unsigned char emitted[10];
static size_t emitted_size;
static int noop(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int call(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"fwrite_probe", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_fwrite_probe_main);
}
static struct cb_stdio_state_v1 *state(void)
{
    ++callbacks;
    real_api->set_errno(CB_EBADF);
    return supplied;
}
static cb_ssize_t count_read(int fd, void *data, size_t size)
{ (void)fd; (void)data; (void)size; ++callbacks; return 0; }
static int count_get(void) { ++callbacks; return CB_ENOENT; }
static void count_set(int error) { (void)error; ++callbacks; }
static int *count_errno(void) { static int value; ++callbacks; return &value; }
static struct cb_input_state_v1 *count_input(void) { ++callbacks; return NULL; }
static cb_ssize_t transfer(int fd, const void *buffer, size_t length)
{
    unsigned index = writes++;
    size_t wanted = 0, accepted = 0;
    ++callbacks;
    if (fd != 1) mismatch = 1;
    real_api->set_errno(CB_EBADF);
    if (plan == 0) { mismatch = 1; return -1; }
    if (plan == 1) { /* Exact 3+5 split, including requested remainder/offset. */
        wanted = index == 0 ? 8 : 5;
        accepted = index == 0 ? 3 : 5;
        if (index > 1) { mismatch = 1; return -1; }
        if (index == 0) first_buffer = buffer;
        if ((const char *)buffer != first_buffer + (index == 0 ? 0 : 3)) mismatch = 1;
    } else if (plan == 2) { /* Partial element error, then independent retry. */
        wanted = index == 0 ? 8 : 2;
        if (index > 2) { mismatch = 1; return -1; }
        if (index == 0) first_buffer = buffer;
        if (index == 1 && (const char *)buffer != first_buffer + 6) mismatch = 1;
        if (length != wanted) mismatch = 1;
        if (index == 1) { real_api->set_errno(CB_EPIPE); return -1; }
        accepted = index == 0 ? 6 : 2;
    } else if (plan == 3) { /* Inspect huge count without reading the buffer. */
        wanted = (uint64_t)SIZE_MAX > (uint64_t)INT64_MAX ? (size_t)INT64_MAX : SIZE_MAX;
        if (index != 0 || length != wanted) mismatch = 1;
        return 0;
    } else if (plan == 4) {
        if (index != 0 || length != 8) mismatch = 1;
        return INT64_C(4294967304); /* Must reject before ILP32 narrowing. */
    } else if (plan == 5) {
        wanted = accepted = 4;
        if (index != 0) mismatch = 1;
    }
    if (length != wanted || emitted_size + accepted > sizeof(emitted)) {
        mismatch = 1; return -1;
    }
    memcpy(emitted + emitted_size, buffer, accepted);
    emitted_size += accepted;
    return (cb_ssize_t)accepted;
}
static void reset(unsigned next_plan)
{
    plan = next_plan; callbacks = writes = mismatch = 0;
    emitted_size = 0; first_buffer = NULL;
    memset(emitted, 0, sizeof(emitted));
}
static int entry(const struct cb_api_v1 *api, int argc,
                 char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    struct cb_stdio_state_v1 good, invalid;
    unsigned char saved[sizeof(invalid)];
    void *short_api = NULL, *short_state = NULL;
    size_t short_size;
    int variant, result = 0;
    (void)argc; (void)argv; (void)envp;
    real_api = api;
    memset(&good, 0, sizeof(good));
    good.abi_version = CB_ABI_VERSION_V1; good.struct_size = sizeof(good);
    copy = *api; copy.write = transfer; copy.stdio_state_location = state;
    supplied = &good;

    /* Every callback relevant to a zero request is observable. The ordinary
       branch itself uses no errno/status API, even through the errno macro. */
    copy.read = count_read; copy.get_errno = count_get; copy.set_errno = count_set;
    copy.errno_location = count_errno; copy.input_state_location = count_input;
    reset(0);
    result = call(&copy, "zero_quiet");
    (void)cb_libc_start(api, 0, NULL, noop);
    if (result || callbacks || writes) return 50;
    copy = *api; copy.write = transfer; copy.stdio_state_location = state;

    for (variant = 0; variant < 7; ++variant) {
        struct cb_api_v1 *bound = &copy;
        copy = *api; copy.write = transfer; copy.stdio_state_location = state;
        invalid = good; invalid.stderr_error = 1; supplied = &invalid;
        if (variant == 0 || variant == 5) {
            short_size = variant == 0 ? offsetof(struct cb_api_v1, stdio_state_location) :
                                        offsetof(struct cb_api_v1, input_state_location);
            short_api = api->allocate(short_size);
            if (short_api == NULL) { result = 51; break; }
            copy.struct_size = short_size;
            memcpy(short_api, &copy, short_size); bound = short_api;
        } else if (variant == 1) copy.stdio_state_location = NULL;
        else if (variant == 2) supplied = NULL;
        else if (variant == 3) invalid.abi_version = 0;
        else if (variant == 4) {
            invalid.struct_size = offsetof(struct cb_stdio_state_v1, stderr_error);
            short_state = api->allocate(invalid.struct_size);
            if (short_state == NULL) { result = 52; break; }
            memcpy(short_state, &invalid, invalid.struct_size); supplied = short_state;
        } else copy.input_state_location = NULL;
        memcpy(saved, &invalid, sizeof(invalid));
        reset(variant < 5 ? 0 : 5);
        result = call(bound, variant < 5 ? "state_old" : "full");
        (void)cb_libc_start(api, 0, NULL, noop);
        if (mismatch || writes != (variant < 5 ? 0u : 1u) ||
            (variant < 2 && callbacks) ||
            memcmp(saved, &invalid, sizeof(invalid)) != 0 ||
            (short_state && memcmp(saved, short_state, invalid.struct_size) != 0)) result = 53;
        if (variant >= 5 && (emitted_size != 4 || memcmp(emitted, "abcd", 4))) result = 54;
        api->release(short_api); short_api = NULL;
        api->release(short_state); short_state = NULL;
        if (result) break;
        /* Recovery happens immediately, without resetting the known state. */
        supplied = &good;
        copy = *api; copy.write = transfer; copy.stdio_state_location = state;
        reset(5); result = call(&copy, "full");
        (void)cb_libc_start(api, 0, NULL, noop);
        if (result || mismatch || writes != 1 || emitted_size != 4 ||
            memcmp(emitted, "abcd", 4) || good.stdout_error || good.stderr_error) {
            result = 55; break;
        }
    }
    api->release(short_api); api->release(short_state);
    if (result) return result;

    for (variant = 0; variant < 5; ++variant) {
        static const char *modes[] = {"split", "partial_recover", "clip", "wide_return", "bounds"};
        static const unsigned expected_writes[] = {2,3,1,1,0};
        good.stdout_error = good.stderr_error = 0; supplied = &good;
        reset(variant < 4 ? (unsigned)variant + 1 : 0);
        result = call(&copy, modes[variant]);
        (void)cb_libc_start(api, 0, NULL, noop);
        if (result || mismatch || writes != expected_writes[variant]) return 60 + variant;
        if (variant == 0 && (emitted_size != 8 || memcmp(emitted, "abcdefgh", 8))) return 65;
        if (variant == 1 && (emitted_size != 8 || memcmp(emitted, "abcdefij", 8))) return 66;
        if (variant >= 2 && emitted_size != 0) return 67;
    }
    return 0;
}
const struct cb_program_v1 cb_fwrite_compat_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "fwritecompat", 0, 64 * 1024, entry
};
