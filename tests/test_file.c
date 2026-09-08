#include "internal.h"
#include "cannedbsd/libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int cb_file_call(const struct cb_api_v1 *, const char *);
extern int cb_file_prepare(const struct cb_api_v1 *);
extern void *stream_probe_handle(unsigned);
extern const struct cb_program_v1 cb_file_probe_program, cb_file_compat_program;
static const struct cb_host_ops_v1 *base;
static const struct cb_api_v1 *real_api;
static struct cb_kernel *kernel;
static struct { void *pointer; size_t size; unsigned releases; } blocks[8192];
static size_t block_count, wrappers[2], files[2];
static struct cb_task *owner;
static int fail_after, mode, exec_fail_at, phase, require_clear;
static int opened[2], open_count, close_count, allocation_count, read_count;
static size_t allocated_before;
enum { PAYLOAD, BOOKKEEPING, CLOSE_ERROR, READ_ERROR, DENIED, EXHAUST,
       EXEC, FAILED_EXEC, FAILED_PREPARE, EXIT, LIVE };
static void fail(const char *text)
{ fprintf(stderr, "FAIL: file ownership %s\n", text); exit(1); }
static size_t block_index(void *pointer)
{
    size_t i;
    for (i = 0; i < block_count; ++i)
        if (blocks[i].pointer == pointer) return i;
    fail("unknown allocation"); return 0;
}
static size_t live_count(void)
{
    size_t i, count = 0;
    for (i = 0; i < block_count; ++i) count += blocks[i].releases == 0;
    return count;
}
static void *allocate(size_t size)
{
    void *pointer;
    if (fail_after == 0) { fail_after = -1; return NULL; }
    if (fail_after > 0) --fail_after;
    pointer = base->allocate(size);
    if (pointer == NULL) return NULL;
    if (block_count == sizeof(blocks)/sizeof(blocks[0])) fail("tracker capacity");
    blocks[block_count].pointer = pointer; blocks[block_count].size = size;
    blocks[block_count++].releases = 0;
    return pointer;
}
static void release(void *pointer)
{
    size_t index;
    if (pointer == NULL) return;
    index = block_index(pointer);
    if (require_clear && (index == wrappers[0] || index == wrappers[1]) &&
        owner->input_state.input_streams != NULL)
        fail("list not cleared before wrapper release");
    if (++blocks[index].releases != 1) fail("duplicate release");
    /* Quarantine until case end so counts cannot be hidden by address reuse. */
}
static void *resize(void *pointer, size_t size)
{
    void *next = allocate(size);
    if (next != NULL && pointer != NULL) {
        size_t old = blocks[block_index(pointer)].size;
        memcpy(next, pointer, size < old ? size : old); release(pointer);
    }
    return next;
}
static int spy_open(const char *path, int flags, uint32_t permissions)
{
    int fd;
    if (flags != CB_O_RDONLY || permissions != 0) fail("fopen open flags");
    fd = real_api->open(path, flags, permissions);
    if (fd >= 0) {
        if (open_count < 2) {
            opened[open_count] = fd;
            files[open_count] = block_index(kernel->current->descriptors[fd].file);
        }
        ++open_count;
    }
    return fd;
}
static int spy_close(int fd)
{
    int result;
    ++close_count; result = real_api->close(fd);
    if (result != 0) fail("spy close underlying result");
    real_api->set_errno(CB_EIO);
    return mode == CLOSE_ERROR ? -1 : 0;
}
static void *spy_allocate(size_t size)
{
    void *result;
    ++allocation_count; allocated_before = block_count;
    if (mode == PAYLOAD || mode == BOOKKEEPING) fail_after = mode == PAYLOAD ? 0 : 1;
    result = real_api->allocate(size);
    return result;
}
static cb_ssize_t spy_read(int fd, void *buffer, size_t count)
{
    int call = read_count++;
    if (fd != opened[0] || count != 1) fail("dynamic read descriptor/count");
    real_api->set_errno(CB_EIO);
    if (call == 0) return -1;
    if (call == 1) { *(unsigned char *)buffer = 'R'; return 1; }
    return 0;
}
static int denied_open(struct cb_vfs_node *node, struct cb_task *task, int flags,
                       struct cb_open_file **file)
{ (void)node; (void)task; (void)flags; (void)file; return -CB_EACCES; }
static void expect_wrappers(unsigned count)
{
    if (blocks[wrappers[0]].releases != count || blocks[wrappers[1]].releases != count)
        fail("wrapper release count");
}
static int simple(const struct cb_api_v1 *api)
{
    struct cb_api_v1 copy = *api;
    struct cb_input_state_v1 *state = api->input_state_location();
    size_t before;
    void *head;
    int result, fd, fds[CB_MAX_FDS], count = 0;
    unsigned char byte;
    real_api = api;
    if (cb_file_prepare(api) != 0) return 70;
    if (mode == PAYLOAD || mode == BOOKKEEPING) {
        if (cb_file_call(api, "open-one") != 0) return 71;
        head = state->input_streams; before = live_count();
        copy.open = spy_open; copy.close = spy_close; copy.allocate = spy_allocate;
        result = cb_file_call(&copy, "nomem");
        if (cb_file_call(api, "clean-stdin") != 0) return 72;
        if (result != 0 || fail_after != -1 || open_count != 1 || close_count != 1 ||
            allocation_count != 1 || state->input_streams != head ||
            kernel->current->descriptors[opened[0]].file != NULL ||
            blocks[files[0]].releases != 1 || live_count() != before) return 73;
        if (mode == BOOKKEEPING && (block_count != allocated_before + 1 ||
            blocks[allocated_before].releases != 1)) return 74;
        return cb_file_call(api, "read-one") || cb_file_call(api, "close-one");
    }
    if (mode == DENIED) {
        struct cb_vfs_node *node;
        const struct cb_vfs_node_ops *original;
        struct cb_vfs_node_ops denying;
        fd = api->open("/tmp/stream-input", CB_O_RDONLY, 0);
        if (fd < 0) return 75;
        node = kernel->current->descriptors[fd].file->object.node;
        original = node->ops; denying = *original; denying.open = denied_open;
        before = live_count(); node->ops = &denying;
        copy.allocate = spy_allocate;
        result = cb_file_call(&copy, "denied");
        node->ops = original;
        if (cb_file_call(api, "clean-stdin") != 0 || result != 0 ||
            allocation_count != 0 || state->input_streams != NULL || live_count() != before)
            return 76;
        return api->close(fd);
    }
    if (mode == EXHAUST) {
        while ((fd = api->open("/tmp/stream-input", CB_O_RDONLY, 0)) >= 0) {
            if (count == CB_MAX_FDS) return 77;
            fds[count++] = fd;
        }
        if (count != CB_MAX_FDS - 3 || api->get_errno() != CB_EMFILE) return 78;
        before = live_count(); copy.allocate = spy_allocate;
        result = cb_file_call(&copy, "emfile");
        if (cb_file_call(api, "clean-stdin") != 0 || result != 0 ||
            allocation_count != 0 || state->input_streams != NULL || live_count() != before)
            return 79;
        while (count > 0) if (api->close(fds[--count]) != 0) return 80;
        return 0;
    }
    copy.open = spy_open;
    if (cb_file_call(&copy, "open-one") != 0) return 81;
    wrappers[0] = block_index(stream_probe_handle(0));
    if (mode == CLOSE_ERROR) {
        copy.close = spy_close;
        result = cb_file_call(&copy, "close-error");
        if (cb_file_call(api, "clean-stdin") != 0 || result != 0 || close_count != 1 ||
            state->input_streams != NULL || blocks[wrappers[0]].releases != 1 ||
            blocks[files[0]].releases != 1) return 82;
        fd = api->open("/tmp/stream-input", CB_O_RDONLY, 0);
        if (fd != opened[0] || cb_file_call(&copy, "closed-again") != 0) return 83;
        if (cb_file_call(api, "clean-stdin") != 0 || close_count != 1 ||
            api->read(fd, &byte, 1) != 1 || byte != 0) return 84;
        return api->close(fd);
    }
    copy.read = spy_read;
    result = cb_file_call(&copy, "read-error");
    if (cb_file_call(api, "clean-stdin") != 0 || result != 0 || read_count != 3)
        return 85;
    return cb_file_call(api, "close-one");
}
static int child_entry(const struct cb_api_v1 *api, int argc,
                        char *const argv[], char *const envp[])
{
    char *next[] = {(char *)"filechild", (char *)"after", NULL};
    struct cb_api_v1 copy = *api;
    struct cb_input_state_v1 *state = api->input_state_location();
    void *head;
    size_t before;
    unsigned char byte;
    (void)argc;
    if (strcmp(argv[1], "after") == 0) {
        expect_wrappers(1);
        if (state->input_streams != NULL || state->stdin_closed ||
            state->stdin_eof || state->stdin_error ||
            blocks[files[0]].releases != 0 || blocks[files[1]].releases != 1 ||
            api->read(opened[0], &byte, 1) != 1 || byte != 0 ||
            api->read(opened[1], &byte, 1) != -1 || api->get_errno() != CB_EBADF ||
            api->close(opened[0]) != 0) return 86;
        phase = 2; return 0;
    }
    real_api = api; owner = kernel->current; copy.open = spy_open;
    if (cb_file_call(&copy, "open-two") != 0 || open_count != 2 ||
        cb_file_call(api, "close-stdin") != 0) return 87;
    wrappers[0] = block_index(stream_probe_handle(0));
    wrappers[1] = block_index(stream_probe_handle(1));
    require_clear = 1;
    if (api->open("/tmp/stream-input", CB_O_RDONLY, 0) != 0 ||
        api->set_cloexec(opened[1], 1) != 0) return 88;
    head = state->input_streams;
    if (mode == EXEC) { api->exec("filechild", next, envp); return 89; }
    if (mode == FAILED_EXEC || mode == FAILED_PREPARE) {
        before = live_count();
        if (mode == FAILED_PREPARE) fail_after = exec_fail_at;
        if (api->exec(mode == FAILED_EXEC ? "missing-file-child" : "filechild", next, envp) != -1 ||
            api->get_errno() != (mode == FAILED_EXEC ? CB_ENOENT : CB_ENOMEM)) return 91;
        if (state->input_streams != head || !state->stdin_closed || live_count() != before ||
            (mode == FAILED_PREPARE && fail_after != -1)) return 92;
        expect_wrappers(0);
        if (cb_file_call(api, "read-two") != 0) return 93;
    }
    if (mode == LIVE) {
        phase = 1;
        for (;;) api->yield();
    }
    phase = 2; return 0;
}
static const struct cb_program_v1 child_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "filechild", 0, 64 * 1024, child_entry
};
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    char *args[] = {(char *)"filechild", (char *)"before", NULL};
    cb_pid_t child;
    unsigned spins;
    int status;
    (void)argc; (void)argv;
    if (mode <= EXHAUST) return simple(api);
    if (cb_file_prepare(api) != 0 || api->spawn("filechild", args, envp, NULL, 0, &child) != 0)
        return 94;
    for (spins = 0; phase == 0 && spins < 30; ++spins) api->yield();
    if (mode == LIVE) { expect_wrappers(0); return phase == 1 ? 0 : 95; }
    if (phase != 2 || owner->state != CB_TASK_ZOMBIE) return 96;
    expect_wrappers(1); /* Observe exit cleanup before wait can reap the task. */
    if (owner->input_state.input_streams != NULL ||
        blocks[files[0]].releases != 1 || blocks[files[1]].releases != 1)
        return 97;
    return api->waitpid(child, &status) == child && status == 0 ? 0 : 98;
}
static const struct cb_program_v1 program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "filetest", 0, 64 * 1024, entry
};
static void run(const struct cb_program_v1 *descriptor, const char *command)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    int status;
    size_t i;
    base = cb_linux_host_ops(); host.allocate = allocate; host.resize = resize; host.release = release;
    block_count = 0; fail_after = -1; phase = require_clear = 0;
    open_count = close_count = allocation_count = read_count = 0;
    kernel = cb_kernel_create(&host);
    if (kernel == NULL) fail("create");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, descriptor) != 0 ||
        cb_kernel_register(kernel, &child_program) != 0 || cb_kernel_boot(kernel, command) != 0)
        fail("registration/boot");
    status = cb_kernel_run(kernel);
    if (status != 0) {
        fprintf(stderr, "%s mode%d failure%d status%d\n", command, mode, exec_fail_at, status);
        fail("ordinary/lifecycle status");
    }
    if (descriptor == &program && mode == LIVE) expect_wrappers(0);
    cb_kernel_destroy(kernel);
    if (descriptor == &program && mode >= EXEC) expect_wrappers(1);
    if (live_count() != 0) fail("remaining allocation after destruction");
    for (i = 0; i < block_count; ++i) base->release(blocks[i].pointer);
}
void cb_test_file(void)
{
    for (mode = PAYLOAD; mode <= LIVE; ++mode) {
        if (mode == FAILED_PREPARE) {
            for (exec_fail_at = 0; exec_fail_at < 7; ++exec_fail_at) run(&program, "filetest");
        } else run(&program, "filetest");
    }
    run(&cb_file_probe_program, "fileprobe");
    run(&cb_file_compat_program, "filecompat");
    puts("file ownership tests passed");
}
