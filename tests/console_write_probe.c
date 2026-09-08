#include "internal.h"
#include <limits.h>

/* Called only from the host/root stack, with no other live kernel. Creating a
   nested kernel would replace the portable runtime's active-kernel binding. */
static const struct cb_host_ops_v1 *base_host;
static unsigned contexts, context_error, calls, mismatch, entered, released;
static void *payload;
static cb_ssize_t answer;
static int expected_stream;
static const void *expected_buffer;
static size_t expected_count;

static struct cb_host_context *root_context(void)
{
    struct cb_host_context *context = base_host->context_root();
    if (context != NULL) ++contexts;
    return context;
}
static struct cb_host_context *create_context(void (*entry)(void *), void *arg,
                                              size_t size)
{
    struct cb_host_context *context = base_host->context_create(entry, arg, size);
    if (context != NULL) ++contexts;
    return context;
}
static void destroy_context(struct cb_host_context *context)
{
    if (context != NULL) {
        if (contexts == 0) context_error = 1;
        else --contexts;
    }
    base_host->context_destroy(context);
}
static void release(void *pointer)
{
    if (payload != NULL && pointer == payload) {
        ++released;
        payload = NULL;
    }
    base_host->release(pointer);
}
static cb_ssize_t fake_write(int stream, const void *buffer, size_t count)
{
    ++calls;
    if (stream != expected_stream || buffer != expected_buffer || count != expected_count)
        mismatch = 1;
    return answer;
}
static int entry(const struct cb_api_v1 *api, int argc,
                 char *const argv[], char *const envp[])
{
    static const unsigned char bytes[] = {0,255,65};
    static const struct {
        cb_ssize_t host_result, result;
        int error;
    } cases[] = {
        {0, -1, CB_EIO}, /* The first actual behavioral red on the old core. */
        {3, 3, CB_ENOENT},
        {1, 1, CB_ENOENT},
        {4, -1, CB_EIO},
        {INT64_C(4294967299), -1, CB_EIO},
        {-CB_EPIPE, -1, CB_EPIPE},
        {-(cb_ssize_t)INT_MAX, -1, INT_MAX},
        {-(cb_ssize_t)INT_MAX - 1, -1, CB_EIO},
        {INT64_MIN, -1, CB_EIO},
        {-1, -1, 1}
    };
    size_t i;
    (void)argc; (void)argv; (void)envp;
    ++entered;
    payload = api->allocate(17);
    if (payload == NULL) return 10;
    expected_stream = 1; expected_buffer = bytes; expected_count = sizeof(bytes);
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        cb_ssize_t result;
        answer = cases[i].host_result; calls = mismatch = 0;
        api->set_errno(CB_ENOENT);
        result = api->write(1, bytes, sizeof(bytes));
        if (result != cases[i].result || api->get_errno() != cases[i].error ||
            calls != 1 || mismatch) return 11 + (int)i;
    }
    answer = INT64_MIN; calls = mismatch = 0;
    api->set_errno(CB_EPIPE);
    if (api->write(1, (const void *)1, 0) != 0 || api->get_errno() != CB_EPIPE ||
        api->write(2, NULL, 0) != 0 || api->get_errno() != CB_EPIPE || calls) return 30;
    if (api->write(-1, NULL, 0) != -1 || api->get_errno() != CB_EBADF ||
        api->write(0, NULL, 0) != -1 || api->get_errno() != CB_EBADF || calls) return 31;
    if (api->write(1, NULL, 3) != -1 || api->get_errno() != CB_EINVAL || calls) return 32;
    if ((uint64_t)SIZE_MAX > (uint64_t)INT64_MAX &&
        (api->write(1, bytes, SIZE_MAX) != -1 || api->get_errno() != CB_EINVAL || calls)) return 33;
    if (api->close(1) != 0 || api->write(1, NULL, 0) != -1 ||
        api->get_errno() != CB_EBADF || calls) return 34;
    expected_stream = 2; answer = 3; calls = mismatch = 0;
    api->set_errno(CB_EPIPE);
    if (api->write(2, bytes, sizeof(bytes)) != 3 || api->get_errno() != CB_EPIPE ||
        calls != 1 || mismatch) return 35;
    return 0;
}
static const struct cb_program_v1 program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "consolewrite", 0,
    64 * 1024, entry
};

int cb_console_write_probe(const struct cb_host_ops_v1 *host)
{
    struct cb_host_ops_v1 copy = *host;
    struct cb_kernel *kernel;
    int result = -1, cleaned_before_destroy;
    base_host = host;
    contexts = context_error = calls = mismatch = entered = released = 0;
    payload = NULL;
    copy.context_root = root_context; copy.context_create = create_context;
    copy.context_destroy = destroy_context; copy.release = release;
    copy.console_write = fake_write;
    kernel = cb_kernel_create(&copy);
    if (kernel == NULL) return -1;
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &program) == 0 &&
        cb_kernel_boot(kernel, "consolewrite") == 0)
        result = cb_kernel_run(kernel);
    cleaned_before_destroy = entered == 1 && released == 1 && payload == NULL;
    cb_kernel_destroy(kernel);
    if (!cleaned_before_destroy || contexts != 0 || context_error) return -2;
    return result;
}
