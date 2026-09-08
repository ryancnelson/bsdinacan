#ifndef CANNEDBSD_HARNESS_H
#define CANNEDBSD_HARNESS_H

#include <cannedbsd/abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// The implementation of the reusable conformance harness
// It is intended to be included in host port test suites.

struct cb_harness_oversized_host {
    struct cb_host_ops_v1 core;
    void (*future_callback)(void);
};

struct cb_harness_mock_context {
    void (*entry)(void *);
    void *arg;
    int is_root;
};

static void cb_harness_fail(const char *reason) {
    fprintf(stderr, "FAIL: %s\n", reason);
    exit(1);
}

static void cb_harness_expect_invalid_host(
    struct cb_kernel *(*create_fn)(const struct cb_host_ops_v1 *),
    const struct cb_host_ops_v1 *host, const char *reason)
{
    struct cb_kernel *kernel = create_fn(host);
    if (kernel != NULL) {
        cb_harness_fail(reason);
}
}

static void *cb_harness_mock_allocate(size_t size) { return malloc(size); }
static void *cb_harness_mock_resize(void *ptr, size_t size) { return realloc(ptr, size); }
static void cb_harness_mock_release(void *ptr) { free(ptr); }
static struct cb_host_context *cb_harness_mock_context_root(void) { return (struct cb_host_context *)1; }
static struct cb_host_context *cb_harness_mock_context_create(void (*entry)(void *), void *arg, size_t stack_size) { (void)entry; (void)arg; (void)stack_size; return NULL; }
static void cb_harness_mock_context_switch(struct cb_host_context *from, struct cb_host_context *to) { (void)from; (void)to; }
static void cb_harness_mock_context_destroy(struct cb_host_context *ctx) { (void)ctx; }
static int cb_harness_mock_console_poll(int timeout_ms) { (void)timeout_ms; return 0; }
static cb_ssize_t cb_harness_mock_console_read(void *buf, size_t count) { (void)buf; (void)count; return 0; }
static cb_ssize_t cb_harness_mock_console_write(int stream, const void *buf, size_t count) { (void)stream; (void)buf; return (cb_ssize_t)count; }
static uint64_t cb_harness_mock_monotonic(void) { return 1000; }
static uint64_t cb_harness_mock_wall(void) { return 1000000; }
static uint64_t cb_harness_mock_zero_clock(void) { return 0; }
static void cb_harness_mock_yield(void) { }
static void cb_harness_mock_fatal(const char *msg) { (void)msg; exit(1); }

static inline void cb_harness_run_mock_api_validation(
    struct cb_kernel *(*create_fn)(const struct cb_host_ops_v1 *),
    void (*destroy_fn)(struct cb_kernel *))
{
    struct cb_host_ops_v1 host;
    struct cb_harness_oversized_host oversized;
    struct cb_kernel *kernel;

    const struct cb_host_ops_v1 mock_host_ops = {
        CB_ABI_VERSION_V1,
        sizeof(struct cb_host_ops_v1),
        cb_harness_mock_allocate,
        cb_harness_mock_resize,
        cb_harness_mock_release,
        cb_harness_mock_context_root,
        cb_harness_mock_context_create,
        cb_harness_mock_context_switch,
        cb_harness_mock_context_destroy,
        cb_harness_mock_console_poll,
        cb_harness_mock_console_read,
        cb_harness_mock_console_write,
        cb_harness_mock_monotonic,
        cb_harness_mock_wall,
        cb_harness_mock_yield,
        cb_harness_mock_fatal
};

    cb_harness_expect_invalid_host(create_fn, NULL, "null table");
    host = mock_host_ops;
    host.abi_version = 0;
    cb_harness_expect_invalid_host(create_fn, &host, "version");
    host = mock_host_ops;
    host.struct_size = sizeof(host) - 1;
    cb_harness_expect_invalid_host(create_fn, &host, "size");

    memset(&oversized, 0, sizeof(oversized));
    oversized.core = mock_host_ops;
    oversized.core.struct_size = sizeof(oversized);
    kernel = create_fn(&oversized.core);
    if (kernel == NULL)
        cb_harness_fail("Oversized table rejected");
    destroy_fn(kernel);

    // Test permitted case: clocks returning 0

    host = mock_host_ops;
    host.monotonic_millis = cb_harness_mock_zero_clock;
    host.wall_clock_millis = cb_harness_mock_zero_clock;
    kernel = create_fn(&host);
    if (kernel == NULL)
        cb_harness_fail("Permitted case (zero clocks) rejected");
    destroy_fn(kernel);

#define CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(member) do { \
    host = mock_host_ops; \
    host.member = NULL; \
    cb_harness_expect_invalid_host(create_fn, &host, #member); \
} while (0)
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(allocate);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(resize);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(release);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(context_root);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(context_create);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(context_switch);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(context_destroy);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(console_poll);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(console_read);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(console_write);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(monotonic_millis);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(wall_clock_millis);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(yield_host);
    CB_HARNESS_EXPECT_NULL_HOST_CALLBACK(fatal);
#undef CB_HARNESS_EXPECT_NULL_HOST_CALLBACK

}

static struct cb_host_context *cb_harness_test_root_ctx;
static struct cb_host_context *cb_harness_test_child_ctx;
static const struct cb_host_ops_v1 *cb_harness_test_adapter;
static int cb_harness_test_child_step;
static int cb_harness_test_parent_step;

static void cb_harness_test_context_entry(void *arg)
{
    int local_state = 42;

    if (arg != (void *)0x1234)
        cb_harness_fail("Context argument mismatch");

    if (cb_harness_test_child_step != 0)
        cb_harness_fail("Context started at wrong step");

    cb_harness_test_child_step = 1;
    local_state++;

    cb_harness_test_adapter->context_switch(cb_harness_test_child_ctx, cb_harness_test_root_ctx);

    if (cb_harness_test_child_step != 1 || cb_harness_test_parent_step != 1)
        cb_harness_fail("Context resumed out of order");
    if (local_state != 43)
        cb_harness_fail("Context local state not preserved");

    cb_harness_test_child_step = 2;
    local_state++;

    cb_harness_test_adapter->context_switch(cb_harness_test_child_ctx, cb_harness_test_root_ctx);

    if (cb_harness_test_child_step != 2)
        cb_harness_fail("Context resumed out of order 2");
    if (local_state != 44)
        cb_harness_fail("Context local state not preserved 2");

    cb_harness_test_child_step = 3;
    cb_harness_test_adapter->context_switch(cb_harness_test_child_ctx, cb_harness_test_root_ctx);
    cb_harness_fail("Context resumed after final exit");
}

static inline void cb_harness_test_real_conformance_contract(const struct cb_host_ops_v1 *adapter)
{
    uint64_t before, after;
    void *ptr1, *ptr2;

    if (adapter == NULL ||
        adapter->abi_version != CB_ABI_VERSION_V1 ||
        adapter->struct_size < sizeof(struct cb_host_ops_v1) ||
        adapter->allocate == NULL || adapter->resize == NULL ||
        adapter->release == NULL || adapter->context_root == NULL ||
        adapter->context_create == NULL ||
        adapter->context_switch == NULL ||
        adapter->context_destroy == NULL ||
        adapter->console_poll == NULL ||
        adapter->console_read == NULL ||
        adapter->console_write == NULL ||
        adapter->monotonic_millis == NULL ||
        adapter->wall_clock_millis == NULL ||
        adapter->yield_host == NULL || adapter->fatal == NULL)
        cb_harness_fail("Host operation table incomplete or invalid");

    // Clocks
    before = adapter->monotonic_millis();
    adapter->yield_host();
    after = adapter->monotonic_millis();
    if (before != 0 && after != 0 && after < before)
        cb_harness_fail("Host clocks behave incorrectly");

    // Allocation
    ptr1 = adapter->allocate(100);
    if (!ptr1) cb_harness_fail("Allocation failed");
    memset(ptr1, 0xAA, 100);
    ptr2 = adapter->resize(ptr1, 200);
    if (!ptr2) cb_harness_fail("Resize failed");
    if (((unsigned char *)ptr2)[0] != 0xAA || ((unsigned char *)ptr2)[99] != 0xAA)
        cb_harness_fail("Resize did not preserve memory");
    adapter->release(ptr2);

    // Console contract
    {
        int events = adapter->console_poll(0);
        (void)events; // Check it executes without crashing
        cb_ssize_t w = adapter->console_write(1, "", 0);
        (void)w; // Check it executes without crashing
    }

    // Context switching conformance
    cb_harness_test_adapter = adapter;
    cb_harness_test_child_step = 0;
    cb_harness_test_parent_step = 0;

    cb_harness_test_root_ctx = adapter->context_root();
    cb_harness_test_child_ctx = adapter->context_create(cb_harness_test_context_entry, (void *)0x1234, 65536);
    if (!cb_harness_test_root_ctx || !cb_harness_test_child_ctx)
        cb_harness_fail("Host context creation failed");

    adapter->context_switch(cb_harness_test_root_ctx, cb_harness_test_child_ctx);
    if (cb_harness_test_child_step != 1) cb_harness_fail("Child did not run to step 1");

    cb_harness_test_parent_step = 1;

    adapter->context_switch(cb_harness_test_root_ctx, cb_harness_test_child_ctx);
    if (cb_harness_test_child_step != 2) cb_harness_fail("Child did not run to step 2");

    cb_harness_test_parent_step = 2;

    adapter->context_switch(cb_harness_test_root_ctx, cb_harness_test_child_ctx);
    if (cb_harness_test_child_step != 3) cb_harness_fail("Child did not run to step 3");

    adapter->context_destroy(cb_harness_test_child_ctx);
    adapter->context_destroy(cb_harness_test_root_ctx); // FIX: Destroy root context too!
}

#ifdef __cplusplus
}
#endif

#endif // CANNEDBSD_HARNESS_H
