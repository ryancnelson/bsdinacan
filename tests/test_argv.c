#include "internal.h"
#include "cannedbsd/libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_argv_probe_main(int, char **);
/* Quarantine released blocks until each case ends. This prevents address reuse
   from obscuring duplicate release or precise pre-teardown lifetime checks. */
static struct { void *pointer; size_t size; unsigned releases; } blocks[4096];
static size_t block_count;
static int fail_after = -1;
static size_t watched[5]; /* vector, three original strings, replacement */
static int mode, phase, exec_fail_at;
static const struct cb_host_ops_v1 *base;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: argv ownership %s\n", message);
    exit(1);
}
static size_t find_block(void *pointer)
{
    size_t i;
    for (i = 0; i < block_count; ++i)
        if (blocks[i].pointer == pointer) return i;
    fail("unknown allocator pointer");
    return 0;
}
static void *allocate(size_t size)
{
    void *pointer;
    if (fail_after == 0) { fail_after = -1; return NULL; }
    if (fail_after > 0) --fail_after;
    pointer = base->allocate(size);
    if (pointer == NULL) return NULL;
    if (block_count == sizeof(blocks) / sizeof(blocks[0])) fail("tracker capacity");
    blocks[block_count].pointer = pointer;
    blocks[block_count].size = size;
    blocks[block_count++].releases = 0;
    return pointer;
}
static void release(void *pointer)
{
    if (pointer != NULL && ++blocks[find_block(pointer)].releases != 1)
        fail("duplicate release");
}
static void *resize(void *pointer, size_t size)
{
    void *next = allocate(size);
    if (next != NULL && pointer != NULL) {
        size_t old = blocks[find_block(pointer)].size;
        memcpy(next, pointer, old < size ? old : size);
        release(pointer);
    }
    return next;
}
static size_t live_count(void)
{
    size_t i, live = 0;
    for (i = 0; i < block_count; ++i)
        if (blocks[i].releases == 0) ++live;
    return live;
}
static void expect_releases(unsigned originals, unsigned replacement)
{
    size_t i;
    for (i = 0; i < 4; ++i)
        if (blocks[watched[i]].releases != originals)
            fail("original vector/string lifetime");
    if (blocks[watched[4]].releases != replacement)
        fail("replacement lifetime");
}
static int child_main(const struct cb_api_v1 *api, int argc,
                       char *const argv[], char *const envp[])
{
    char *next[] = {(char *)"after", NULL, NULL};
    const char *startup = api->getprogname();
    size_t before, i;
    if (strcmp(argv[0], "after") == 0) {
        expect_releases(1, 1); /* before replacement exit or parent reap */
        if (argc != 2 || strcmp(argv[1], "replacement argument") != 0 ||
            argv[1] == blocks[watched[4]].pointer || strcmp(startup, "after") != 0)
            return 20;
        phase = 2;
        return 0;
    }
    if (argc != 3) return 21;
    watched[0] = find_block((void *)argv);
    for (i = 0; i < 3; ++i) watched[i + 1] = find_block(argv[i]);
    if (cb_libc_start(api, argc, argv, cb_argv_probe_main) != 0) return 22;
    watched[4] = find_block(argv[1]);
    next[1] = argv[1];
    if (mode == 1) ((char **)argv)[0] = NULL;
    if (mode == 2) {
        ((char **)argv)[0] = argv[1];
        ((char **)argv)[2] = argv[1];
    }
    if (api->getprogname() != startup || strcmp(startup, "argvchild") != 0)
        return 23;
    expect_releases(0, 0);
    if (mode == 3) { api->exec("argvchild", next, envp); return 24; }
    if (mode == 4) {
        if (api->exec("missing-argv-command", next, envp) != -1 ||
            api->get_errno() != CB_ENOENT) return 25;
        expect_releases(0, 0);
    }
    if (mode == 5) {
        before = live_count();
        fail_after = exec_fail_at;
        if (api->exec("argvchild", next, envp) != -1 ||
            api->get_errno() != CB_ENOMEM) return 26;
        if (fail_after != -1) return 27; /* failure really reached allocation */
        expect_releases(0, 0);
        if (live_count() != before || argv[1] != next[1] ||
            api->getprogname() != startup || strcmp(argv[2], "tail") != 0)
            return 28;
    }
    phase = mode == 6 ? 1 : 2;
    while (mode == 6) api->yield(); /* parent exits; kernel destroys live child */
    return 0;
}
static const struct cb_program_v1 child_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "argvchild", 0,
    64 * 1024, child_main
};
static int parent_main(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    char *args[] = {(char *)"argvchild", (char *)"original", (char *)"tail", NULL};
    cb_pid_t child;
    unsigned spins;
    int status, attempt;
    size_t before;
    (void)argc; (void)argv;
    if (mode == 7) {
        for (attempt = 0; attempt < 32; ++attempt) {
            before = live_count();
            fail_after = attempt;
            status = api->spawn("argvchild", args, envp, NULL, 0, &child);
            if (status == 0) { fail_after = -1; break; }
            if (api->get_errno() != CB_ENOMEM || fail_after != -1 ||
                live_count() != before) return 34;
        }
        if (attempt < 6 || attempt == 32) return 35;
    } else if (api->spawn("argvchild", args, envp, NULL, 0, &child) != 0) return 30;
    for (spins = 0; phase == 0 && spins < 20; ++spins) api->yield();
    if (mode == 6) { expect_releases(0, 0); return phase == 1 ? 0 : 31; }
    if (phase != 2) return 32;
    expect_releases(mode == 3 ? 1 : 0, 1); /* exit before reap */
    if (api->waitpid(child, &status) != child || status != 0) return 33;
    expect_releases(1, 1); /* wait cleanup, before parent exit/teardown */
    return 0;
}
static const struct cb_program_v1 parent_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "argvparent", 0,
    64 * 1024, parent_main
};
static void run_case(void)
{
    struct cb_host_ops_v1 host;
    struct cb_kernel *kernel;
    size_t i;
    int status;
    base = cb_linux_host_ops(); host = *base;
    host.allocate = allocate; host.resize = resize; host.release = release;
    block_count = 0; phase = 0; fail_after = -1;
    kernel = cb_kernel_create(&host);
    if (kernel == NULL) fail("create");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &child_program) != 0 ||
        cb_kernel_register(kernel, &parent_program) != 0 ||
        cb_kernel_boot(kernel, "argvparent") != 0) fail("registration/boot");
    status = cb_kernel_run(kernel);
    if (status != 0) {
        fprintf(stderr, "mode %d allocation %d status %d\n", mode, exec_fail_at, status);
        fail("program status");
    }
    expect_releases(mode == 6 ? 0 : 1, mode == 6 ? 0 : 1);
    cb_kernel_destroy(kernel);
    expect_releases(1, 1);
    if (live_count() != 0) fail("unreleased allocation after destroy");
    for (i = 0; i < block_count; ++i) base->release(blocks[i].pointer);
}
void cb_test_argv(void)
{
    for (mode = 0; mode <= 7; ++mode) {
        if (mode == 5) {
            /* Two new arguments: ownership vector + strings + public vector;
               inherited two-entry environment: vector + two strings. */
            for (exec_fail_at = 0; exec_fail_at < 7; ++exec_fail_at) run_case();
        } else run_case();
    }
    puts("argv ownership tests passed");
}
