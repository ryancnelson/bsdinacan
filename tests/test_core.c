#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct cb_program_v1 cb_stdio_state_probe_program;
extern const struct cb_program_v1 cb_argv_probe_program;
extern const struct cb_program_v1 cb_stdio_oldtable_program;
extern const struct cb_program_v1 cb_exitprobe_program;
extern const struct cb_program_v1 cb_progname_probe_program;
extern const struct cb_program_v1 cb_vfs_executable_probe_program;
extern const struct cb_program_v1 cb_getoptprobe_program;
extern const struct cb_program_v1 cb_getopt_arg_probe_program;
extern const struct cb_program_v1 cb_errxprobe_program;
extern const struct cb_program_v1 cb_err_probe_program;
extern const struct cb_program_v1 cb_strcpy_probe_program;
extern int cb_strcpy_probe_main(int argc, char **argv);
extern int cb_err_probe_main(int argc, char **argv);
extern const struct cb_program_v1 cb_dirname_probe_program;
extern int cb_dirname_probe_main(int argc, char *argv[]);
extern int cb_dirname_oldtable_main(int argc, char *argv[]);
extern const struct cb_program_v1 cb_basename_probe_program;
extern int cb_basename_probe_main(int argc, char *argv[]);
extern int cb_basename_oldtable_main(int argc, char *argv[]);

extern const struct cb_program_v1 cb_direntprobe_program;
extern int cb_direntoldtable_main(int argc, char *argv[]);
extern int cb_direntallocfail_main(int argc, char *argv[]);
extern int cb_direntreaddirunavail_main(int argc, char *argv[]);
extern int dirent_rebind_open(int argc, char *argv[]);
extern int dirent_rebind_readdir(int argc, char *argv[]);
extern int dirent_rebind_closedir_reject(int argc, char *argv[]);
extern int dirent_rebind_closedir_accept(int argc, char *argv[]);

static char captured[32768];
static size_t captured_size;
static char captured_streams[3][32768];
static size_t captured_stream_sizes[3];
static size_t capture_write_limit = (size_t)-1;
static void *(*base_allocate)(size_t);
static void *(*base_resize)(void *, size_t);
static void (*base_release)(void *);
static int allocation_failure_countdown = -1;
static int resize_failure_countdown = -1;
static size_t resize_request_size;
static struct cb_kernel *dir_reclaim_probe_kernel;
static size_t dir_reclaim_exit_before_references = (size_t)-1;
static size_t dir_reclaim_exit_after_references = (size_t)-1;
static size_t dir_reclaim_exec_peer_references = (size_t)-1;
static struct cb_kernel *truncate_test_kernel;
static int truncate_child_phase;
static long allocation_balance;
static const unsigned char *console_input;
static size_t console_input_size;
static size_t console_input_position;
static int console_poll_calls;
static int console_poll_ready;
static int console_read_error;
static int terminal_peer_ran;
static int descriptor_child_fd;
static cb_pid_t process_expected_parent;
static int process_child_phase;
static int pipe_zero_peer_ran;
static int pipe_edge_read_fd;
static int pipe_edge_peer_state;
static int pipe_capacity_read_fd;
static int pipe_capacity_peer_started;
static size_t pipe_capacity_bytes_read;
static int environ_pipe_read_fd;
static int environ_peer_started;
static size_t environ_peer_bytes_read;
static int errno_child_phase;
static int *errno_child_address;
static int allocation_child_phase;
static void *allocation_foreign_pointer;
static const struct cb_executor_ops *executor_delegate;
static unsigned executor_prepare_count;
static unsigned executor_create_count;
static unsigned executor_resume_count;
static unsigned executor_suspend_count;
static unsigned executor_terminate_count;
static unsigned executor_instance_destroy_count;
static unsigned executor_program_destroy_count;
static void fail(const char *message);
extern const struct cb_program_v1 cb_truncate_probe_program;
extern const struct cb_program_v1 cb_memory_probe_program;

static int registration_stub_main(const struct cb_api_v1 *api, int argc,
                                  char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    return 0;
}

static int lifecycle_prepare(struct cb_kernel *kernel,
                             const struct cb_executor_ops *executor,
                             const void *source,
                             struct cb_program **program_out)
{
    ++executor_prepare_count;
    return executor_delegate->prepare(kernel, executor, source, program_out);
}

static struct cb_execution *lifecycle_instance_create(
    struct cb_task *task, const struct cb_program *program)
{
    struct cb_vfs_node *node;
    if (cb_vfs_lookup_node(task, "/bin/sh", &node) == 0) {
        node->ops->unlink(node);
    }
    ++executor_create_count;
    return executor_delegate->instance_create(task, program);
}

static void lifecycle_start_or_resume(struct cb_execution *execution)
{
    ++executor_resume_count;
    executor_delegate->start_or_resume(execution);
}

static void lifecycle_suspend(struct cb_execution *execution)
{
    ++executor_suspend_count;
    executor_delegate->suspend(execution);
}

static void lifecycle_request_termination(struct cb_execution *execution)
{
    ++executor_terminate_count;
    executor_delegate->request_termination(execution);
}

static void lifecycle_instance_destroy(struct cb_execution *execution)
{
    ++executor_instance_destroy_count;
    executor_delegate->instance_destroy(execution);
}

static void lifecycle_program_destroy(struct cb_kernel *kernel,
                                      struct cb_program *program)
{
    ++executor_program_destroy_count;
    executor_delegate->program_destroy(kernel, program);
}

static int executor_lifecycle_main(const struct cb_api_v1 *api, int argc,
                                   char *const argv[], char *const envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    if (executor_resume_count != 1 || executor_suspend_count != 0)
        return 189;
    api->yield();
    if (executor_resume_count != 2 || executor_suspend_count != 1)
        return 190;
    return 0;
}

static void *controlled_allocate(size_t size)
{
    if (allocation_failure_countdown == 0) {
        allocation_failure_countdown = -1;
        return NULL;
    }
    if (allocation_failure_countdown > 0)
        --allocation_failure_countdown;
    return base_allocate(size);
}

static void *controlled_resize(void *pointer, size_t size)
{
    resize_request_size = size;
    if (resize_failure_countdown == 0) {
        resize_failure_countdown = -1;
        return NULL;
    }
    if (resize_failure_countdown > 0)
        --resize_failure_countdown;
    return base_resize(pointer, size);
}

static void *tracked_allocate(size_t size)
{
    void *pointer = base_allocate(size);
    if (pointer != NULL)
        ++allocation_balance;
    return pointer;
}

static void *dirty_allocate(size_t size)
{
    void *pointer = base_allocate(size);
    if (pointer != NULL)
        memset(pointer, 0xa5, size);
    return pointer;
}

static void *tracked_resize(void *pointer, size_t size)
{
    void *resized = base_resize(pointer, size);
    if (pointer == NULL && resized != NULL)
        ++allocation_balance;
    return resized;
}

static void tracked_release(void *pointer)
{
    if (pointer != NULL) {
        if (allocation_balance <= 0)
            fail("allocation tracker underflow");
        --allocation_balance;
    }
    base_release(pointer);
}

static cb_ssize_t capture_write(int stream, const void *buffer, size_t count)
{
    if (count > capture_write_limit)
        count = capture_write_limit;
    if (count > sizeof(captured) - captured_size - 1)
        return -CB_ENOSPC;
    if (stream < 0 || stream >= 3 ||
        count > sizeof(captured_streams[stream]) -
                captured_stream_sizes[stream] - 1)
        return -CB_ENOSPC;
    memcpy(captured + captured_size, buffer, count);
    captured_size += count;
    captured[captured_size] = '\0';
    memcpy(captured_streams[stream] + captured_stream_sizes[stream], buffer,
           count);
    captured_stream_sizes[stream] += count;
    captured_streams[stream][captured_stream_sizes[stream]] = '\0';
    return (cb_ssize_t)count;
}

static int controlled_console_poll(int timeout_ms)
{
    (void)timeout_ms;
    ++console_poll_calls;
    return console_poll_ready;
}

static cb_ssize_t controlled_console_read(void *buffer, size_t count)
{
    size_t remaining;
    if (console_read_error)
        return -CB_EIO;
    remaining = console_input_size - console_input_position;
    if (count > remaining)
        count = remaining;
    if (count != 0)
        memcpy(buffer, console_input + console_input_position, count);
    console_input_position += count;
    return (cb_ssize_t)count;
}

static void reset_console(const char *input)
{
    size_t stream;
    captured_size = 0;
    captured[0] = '\0';
    for (stream = 0; stream < 3; ++stream) {
        captured_stream_sizes[stream] = 0;
        captured_streams[stream][0] = '\0';
    }
    console_input = (const unsigned char *)(input == NULL ? "" : input);
    console_input_size = strlen((const char *)console_input);
    console_input_position = 0;
    console_poll_calls = 0;
    console_poll_ready = 1;
    console_read_error = 0;
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void expect_path(const char *cwd, const char *path,
                        const char *expected)
{
    char actual[CB_PATH_MAX];
    if (cb_test_path_normalize(cwd, path, actual, sizeof(actual)) < 0 ||
        strcmp(actual, expected) != 0) {
        fprintf(stderr, "path %s + %s: expected <%s>, got <%s>\n",
                cwd, path, expected, actual);
        exit(1);
    }
}

static void test_allocation_cleanup(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;
    base_allocate = host.allocate;
    base_resize = host.resize;
    base_release = host.release;
    allocation_balance = 0;
    host.allocate = tracked_allocate;
    host.resize = tracked_resize;
    host.release = tracked_release;
    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(NULL);
    kernel = cb_kernel_create(&host);
    if (kernel == NULL)
        fail("allocation cleanup kernel creation");
    cb_register_base_programs(kernel);
    if (cb_kernel_boot(kernel,
            "echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result; "
            "echo -n hello | wc -c") < 0)
        fail("allocation cleanup boot");
    status = cb_kernel_run(kernel);
    if (status != 0 || strcmp(captured, "HELLO\n5\n") != 0)
        fail("allocation cleanup acceptance behavior");
    cb_kernel_destroy(kernel);
    if (allocation_balance != 0)
        fail("runtime allocation cleanup");
}

static void test_uninitialized_host_memory(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;
    base_allocate = host.allocate;
    host.allocate = dirty_allocate;
    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(NULL);
    kernel = cb_kernel_create(&host);
    if (kernel == NULL)
        fail("dirty-memory kernel creation");
    cb_register_base_programs(kernel);
    if (cb_kernel_boot(kernel,
            "echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result; "
            "echo -n hello | wc -c") < 0)
        fail("dirty-memory boot");
    status = cb_kernel_run(kernel);
    if (status != 0 || strcmp(captured, "HELLO\n5\n") != 0)
        fail("dirty-memory acceptance behavior");
    cb_kernel_destroy(kernel);
}


#include <cannedbsd/harness.h>

static void test_host_contract(void)
{
    cb_harness_run_mock_api_validation(cb_kernel_create, cb_kernel_destroy);
    cb_harness_test_real_conformance_contract(cb_linux_host_ops());
}

static struct cb_vfs_node *null_mount_root(struct cb_vfs_mount *mount)
{
    (void)mount;
    return NULL;
}

static void expect_invalid_root_mount(struct cb_kernel *kernel,
                                      struct cb_vfs_mount *mount,
                                      const char *field)
{
    if (cb_vfs_set_root_mount(kernel, mount) == 0) {
        fprintf(stderr, "root mount with invalid %s was accepted\n", field);
        exit(1);
    }
}

static void test_vfs_contract(void)
{
    struct cb_kernel kernel;
    struct cb_kernel other_kernel;
    struct cb_vfs_mount *mount;
    struct cb_vfs_node *root;
    struct cb_vfs_node *tmp = NULL;
    const struct cb_vfs_mount_ops *mount_ops;
    const struct cb_vfs_node_ops *node_ops;
    struct cb_vfs_mount_ops mount_copy;
    struct cb_vfs_node_ops node_copy;
    struct cb_stat_v1 stat_buffer;

    memset(&kernel, 0, sizeof(kernel));
    memset(&other_kernel, 0, sizeof(other_kernel));
    kernel.host = cb_linux_host_ops();
    other_kernel.host = cb_linux_host_ops();
    mount = cb_ramfs_mount_create(&kernel);
    if (mount == NULL)
        fail("RAMFS mount creation");
    mount_ops = mount->ops;
    root = mount_ops->root(mount);
    node_ops = root == NULL ? NULL : root->ops;
    if (mount_ops->abi_version != CB_ABI_VERSION_V1 ||
        mount_ops->struct_size != sizeof(*mount_ops) ||
        mount_ops->root == NULL || mount_ops->destroy == NULL ||
        node_ops == NULL || node_ops->abi_version != CB_ABI_VERSION_V1 ||
        node_ops->struct_size != sizeof(*node_ops) ||
        node_ops->retain == NULL || node_ops->release == NULL ||
        node_ops->lookup == NULL || node_ops->create == NULL ||
        node_ops->unlink == NULL || node_ops->open == NULL ||
        node_ops->stat == NULL || node_ops->parent == NULL ||
        node_ops->name == NULL)
        fail("RAMFS VFS operation tables");
    expect_invalid_root_mount(NULL, mount, "null kernel");
    expect_invalid_root_mount(&kernel, NULL, "null mount");
    mount->kernel = &other_kernel;
    expect_invalid_root_mount(&kernel, mount, "kernel ownership");
    mount->kernel = &kernel;

    mount_copy = *mount_ops;
    mount_copy.abi_version = 0;
    mount->ops = &mount_copy;
    expect_invalid_root_mount(&kernel, mount, "mount version");
    mount_copy = *mount_ops;
    mount_copy.struct_size = sizeof(mount_copy) - 1;
    expect_invalid_root_mount(&kernel, mount, "mount size");
    mount_copy = *mount_ops;
    mount_copy.root = NULL;
    expect_invalid_root_mount(&kernel, mount, "mount root callback");
    mount_copy = *mount_ops;
    mount_copy.destroy = NULL;
    expect_invalid_root_mount(&kernel, mount, "mount destroy callback");
    mount_copy = *mount_ops;
    mount_copy.root = null_mount_root;
    expect_invalid_root_mount(&kernel, mount, "null root node");

    mount->ops = mount_ops;
#define EXPECT_INVALID_NODE_OP(member) do { \
    node_copy = *node_ops; \
    node_copy.member = NULL; \
    root->ops = &node_copy; \
    expect_invalid_root_mount(&kernel, mount, "node " #member); \
} while (0)
    node_copy = *node_ops;
    node_copy.abi_version = 0;
    root->ops = &node_copy;
    expect_invalid_root_mount(&kernel, mount, "node version");
    node_copy = *node_ops;
    node_copy.struct_size = offsetof(struct cb_vfs_node_ops, truncate) - 1;
    root->ops = &node_copy;
    expect_invalid_root_mount(&kernel, mount, "node size");
    EXPECT_INVALID_NODE_OP(retain);
    EXPECT_INVALID_NODE_OP(release);
    EXPECT_INVALID_NODE_OP(lookup);
    EXPECT_INVALID_NODE_OP(create);
    EXPECT_INVALID_NODE_OP(unlink);
    EXPECT_INVALID_NODE_OP(open);
    EXPECT_INVALID_NODE_OP(stat);
    EXPECT_INVALID_NODE_OP(parent);
    EXPECT_INVALID_NODE_OP(name);
#undef EXPECT_INVALID_NODE_OP

    node_copy = *node_ops;
    node_copy.struct_size = offsetof(struct cb_vfs_node_ops, truncate);
    root->ops = &node_copy;
    if (cb_vfs_set_root_mount(&kernel, mount) < 0 ||
        cb_vfs_set_root_mount(&kernel, mount) == 0 ||
        kernel.vfs_root != root || root->mount != mount ||
        root->ops->parent(root) != NULL ||
        strcmp(root->ops->name(root), "") != 0 ||
        root->ops->stat(root, &stat_buffer) < 0 ||
        stat_buffer.type != CB_NODE_DIRECTORY || stat_buffer.mode != 0755 ||
        root->ops->lookup(root, "tmp", 3, &tmp) < 0 || tmp == NULL ||
        tmp->mount != mount)
        fail("root mount installation");
    cb_vfs_node_retain(tmp);
    cb_vfs_node_release(tmp);
    root->ops = node_ops;
    cb_vfs_destroy(&kernel);
    if (kernel.root_mount != NULL || kernel.vfs_root != NULL)
        fail("root mount destruction");
}

static void test_truncate_vfs_contract(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    struct cb_task task;
    struct cb_open_file *file;
    struct cb_vfs_node *node;
    const struct cb_vfs_node_ops *original;
    struct cb_vfs_node_ops copy;
    struct cb_vfs_node_ops *old;
    struct cb_stat_v1 status;
    int error = 0;
    size_t old_size = offsetof(struct cb_vfs_node_ops, truncate);
    if (kernel == NULL)
        fail("truncate adapter kernel");
    memset(&task, 0, sizeof(task));
    task.kernel = kernel;
    task.root = task.cwd = kernel->vfs_root;
    task.error_cell = &error;
    file = cb_vfs_open(&task, "/tmp/optional", CB_O_CREAT | CB_O_RDWR, 0600);
    if (file == NULL)
        fail("truncate adapter file");
    node = file->object.node;
    original = node->ops;
    old = malloc(old_size);
    if (old == NULL)
        fail("old VFS table allocation");
    copy = *original;
    copy.struct_size = (uint32_t)old_size;
    memcpy(old, &copy, old_size);
    node->ops = old;
    /* The allocation ends at the old prefix: ASan catches tail-member reads. */
    if (cb_vfs_stat_path(&task, "/tmp/optional", &status) != 0 ||
        cb_vfs_truncate_path(&task, "/tmp/optional", 2) != -1 ||
        error != CB_ENOSYS || file->ops->truncate(file, &task, 2) != -1 ||
        error != CB_EBADF)
        fail("old VFS prefix or absent truncate capability");
    node->ops = original;
    free(old);

    copy = *original;
    node->ops = &copy;
    /* One byte short of fully including `truncate` specifically -- not
       "one byte short of the whole struct", which only ever meant the
       same thing while truncate happened to be the last field. A field
       appended after it (e.g. VFS-03's child_at) must not silently make
       this stop testing truncate's own old-table guard. */
    copy.struct_size = (uint32_t)(offsetof(struct cb_vfs_node_ops, truncate) +
                                  sizeof(copy.truncate) - 1);
    if (cb_vfs_truncate_path(&task, "/tmp/optional", 2) != -1 ||
        error != CB_ENOSYS)
        fail("partial truncate callback must not be read");
    copy.struct_size = sizeof(copy);
    copy.truncate = NULL;
    if (cb_vfs_truncate_path(&task, "/tmp/optional", 2) != -1 ||
        error != CB_ENOSYS || file->ops->truncate(file, &task, 2) != -1 ||
        error != CB_EBADF)
        fail("NULL optional truncate callback");
    copy = *original;
    copy.struct_size = sizeof(copy) + 32;
    if (cb_vfs_truncate_path(&task, "/tmp/optional", 2) != 0 ||
        cb_vfs_stat_path(&task, "/tmp/optional", &status) != 0 ||
        status.size != 2)
        fail("larger VFS table prefix compatibility");

    /* child_at's own old-table guard (VFS-03), same style: a table one
       byte short of fully including child_at must not have it read,
       regardless of node type -- the struct_size check happens before
       any type-specific dispatch. */
    {
        struct cb_vfs_node *unused_child;
        copy = *original;
        node->ops = &copy;
        copy.struct_size = (uint32_t)(offsetof(struct cb_vfs_node_ops,
                                               child_at) +
                                      sizeof(copy.child_at) - 1);
        if (cb_vfs_child_at(node, 0, &unused_child) != -CB_ENOSYS)
            fail("partial child_at callback must not be read");
        copy.struct_size = sizeof(copy);
        copy.child_at = NULL;
        if (cb_vfs_child_at(node, 0, &unused_child) != -CB_ENOSYS)
            fail("NULL optional child_at callback");
    }
    node->ops = original;
    cb_open_file_release(file);
    cb_kernel_destroy(kernel);
}

static void test_registration_contract(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    struct cb_program_v1 candidate = {
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "candidate", 0,
        64 * 1024, registration_stub_main
    };
    struct cb_program_v1 programs[CB_MAX_PROGRAMS];
    char names[CB_MAX_PROGRAMS][24];
    size_t index;

    if (kernel == NULL)
        fail("registration test kernel creation");
    if (cb_kernel_register(NULL, &candidate) == 0 ||
        cb_kernel_register(kernel, NULL) == 0)
        fail("null registration input");
    candidate.abi_version = 0;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("program version validation");
    candidate.abi_version = CB_ABI_VERSION_V1;
    candidate.struct_size = sizeof(candidate) - 1;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("program size validation");
    candidate.struct_size = sizeof(candidate);
    candidate.name = NULL;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("null program name validation");
    candidate.name = "";
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("empty program name validation");
    candidate.name = "candidate";
    candidate.flags = 1;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("program flags validation");
    candidate.flags = 0;
    candidate.start = NULL;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("null program entry validation");

    for (index = 0; index < CB_MAX_PROGRAMS; ++index) {
        snprintf(names[index], sizeof(names[index]), "program-%lu",
                 (unsigned long)index);
        programs[index] = candidate;
        programs[index].name = names[index];
        programs[index].start = registration_stub_main;
        if (cb_kernel_register(kernel, &programs[index]) < 0)
            fail("valid program registration");
        if (cb_kernel_register(kernel, &programs[index]) == 0)
            fail("duplicate program registration");
    }
    candidate.name = "overflow";
    candidate.start = registration_stub_main;
    if (cb_kernel_register(kernel, &candidate) == 0)
        fail("program registry capacity");
    cb_kernel_destroy(kernel);
}

static void expect_invalid_executor(struct cb_kernel *kernel,
                                    const struct cb_executor_ops *executor,
                                    const struct cb_program_v1 *source,
                                    const char *field)
{
    if (cb_kernel_register_executor(kernel, executor, source) == 0) {
        fprintf(stderr, "executor with invalid %s was accepted\n", field);
        exit(1);
    }
}



static unsigned exec_early_destroy_count;
static void exec_early_program_destroy(struct cb_kernel *kernel,
                                       struct cb_program *program)
{
    exec_early_destroy_count++;
    cb_native_executor()->program_destroy(kernel, program);
}

static int exec_early_fail_countdown = -1;
static int exec_early_fail_target;
static struct cb_kernel *exec_early_kernel;

static void *exec_early_allocate(size_t size)
{
    if (exec_early_fail_countdown == 0) {
        exec_early_fail_countdown = -1;
        struct cb_vfs_node *node;

        if (cb_vfs_lookup_node(exec_early_kernel->current, "/bin/early_target", &node) != 0)
            fail("lookup early_target during allocation");

        if (node->ops->unlink(node) != 0)
            fail("unlink early_target during allocation");

        if (exec_early_destroy_count != 0)
            fail("executable destroyed during allocation (not retained early)");

        return NULL;
    }
    if (exec_early_fail_countdown > 0)
        exec_early_fail_countdown--;
    return base_allocate(size);
}

static int early_caller_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    char *exec_argv[] = {(char *)"early_target", NULL};
    char *exec_envp[] = {(char *)"FOO=bar", NULL};
    (void)argc; (void)argv; (void)envp;

    exec_early_fail_countdown = exec_early_fail_target;

    if (api->exec("early_target", exec_argv, exec_envp) >= 0)
        return 1;
    if (api->get_errno() != CB_ENOMEM)
        return 2;
    if (exec_early_destroy_count != 1)
        return 3;
    return 0;
}

static const struct cb_program_v1 early_caller_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "early_caller", 0,
    64 * 1024, early_caller_main
};

static void test_exec_early_retain_case(int fail_countdown)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    base_allocate = host.allocate;
    host.allocate = exec_early_allocate;

    exec_early_fail_countdown = -1;
    exec_early_fail_target = fail_countdown;
    exec_early_destroy_count = 0;

    struct cb_kernel *kernel = cb_kernel_create(&host);
    if (kernel == NULL) fail("kernel creation");

    exec_early_kernel = kernel;
    if (cb_kernel_register(kernel, &early_caller_program) < 0)
        fail("register early_caller");
    cb_register_base_programs(kernel);

    struct cb_executor_ops executor = *cb_native_executor();
    executor.program_destroy = exec_early_program_destroy;
    struct cb_program_v1 target_source = {
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "early_target", 0,
        64 * 1024, early_caller_main
    };

    if (cb_kernel_register_executor(kernel, &executor, &target_source) < 0)
        fail("register early_target");

    if (cb_kernel_boot(kernel, "early_caller") != 0)
        fail("boot early_caller");

    int status = cb_kernel_run(kernel);
    if (status != 0) {
        fprintf(stderr, "early_caller failed with %d (countdown %d)\n", status, fail_countdown);
        fail("early_caller run failed");
    }

    cb_kernel_destroy(kernel);
}

static void test_exec_early_retain(void)
{
    test_exec_early_retain_case(0);
    test_exec_early_retain_case(1);
    test_exec_early_retain_case(2);
    test_exec_early_retain_case(3);
}

static void test_executor_contract(void)
{
    const struct cb_executor_ops *native = cb_native_executor();
    struct cb_executor_ops executor = {
        CB_ABI_VERSION_V1,
        sizeof(struct cb_executor_ops),
        lifecycle_prepare,
        lifecycle_instance_create,
        lifecycle_start_or_resume,
        lifecycle_suspend,
        lifecycle_request_termination,
        lifecycle_instance_destroy,
        lifecycle_program_destroy
    };
    char source_name[] = "sh";
    struct cb_program_v1 source = {
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), source_name, 0,
        64 * 1024, executor_lifecycle_main
    };
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    int status;

    if (native == NULL || native->abi_version != CB_ABI_VERSION_V1 ||
        native->struct_size != sizeof(*native) || native->prepare == NULL ||
        native->instance_create == NULL || native->start_or_resume == NULL ||
        native->suspend == NULL || native->request_termination == NULL ||
        native->instance_destroy == NULL || native->program_destroy == NULL)
        fail("native executor operation table");
    if (kernel == NULL)
        fail("executor test kernel creation");
    expect_invalid_executor(NULL, &executor, &source, "null kernel");
    expect_invalid_executor(kernel, NULL, &source, "null table");
    expect_invalid_executor(kernel, &executor, NULL, "null source");
    executor.abi_version = 0;
    expect_invalid_executor(kernel, &executor, &source, "version");
    executor.abi_version = CB_ABI_VERSION_V1;
    executor.struct_size = sizeof(executor) - 1;
    expect_invalid_executor(kernel, &executor, &source, "size");
#define EXPECT_NULL_EXECUTOR_CALLBACK(member) do { \
    executor = (struct cb_executor_ops){ \
        CB_ABI_VERSION_V1, sizeof(struct cb_executor_ops), \
        lifecycle_prepare, lifecycle_instance_create, \
        lifecycle_start_or_resume, lifecycle_suspend, \
        lifecycle_request_termination, lifecycle_instance_destroy, \
        lifecycle_program_destroy \
    }; \
    executor.member = NULL; \
    expect_invalid_executor(kernel, &executor, &source, #member); \
} while (0)
    EXPECT_NULL_EXECUTOR_CALLBACK(prepare);
    EXPECT_NULL_EXECUTOR_CALLBACK(instance_create);
    EXPECT_NULL_EXECUTOR_CALLBACK(start_or_resume);
    EXPECT_NULL_EXECUTOR_CALLBACK(suspend);
    EXPECT_NULL_EXECUTOR_CALLBACK(request_termination);
    EXPECT_NULL_EXECUTOR_CALLBACK(instance_destroy);
    EXPECT_NULL_EXECUTOR_CALLBACK(program_destroy);
#undef EXPECT_NULL_EXECUTOR_CALLBACK

    executor = (struct cb_executor_ops){
        CB_ABI_VERSION_V1, sizeof(struct cb_executor_ops),
        lifecycle_prepare, lifecycle_instance_create,
        lifecycle_start_or_resume, lifecycle_suspend,
        lifecycle_request_termination, lifecycle_instance_destroy,
        lifecycle_program_destroy
    };
    executor_delegate = native;
    executor_prepare_count = 0;
    executor_create_count = 0;
    executor_resume_count = 0;
    executor_suspend_count = 0;
    executor_terminate_count = 0;
    executor_instance_destroy_count = 0;
    executor_program_destroy_count = 0;
    if (cb_kernel_register_executor(kernel, &executor, &source) < 0)
        fail("executor lifecycle setup");
    strcpy(source_name, "xx");
    source.start = NULL;
    if (cb_kernel_boot(kernel, NULL) < 0)
        fail("prepared program source ownership");
    status = cb_kernel_run(kernel);
    if (status != 0 || executor_prepare_count != 1 ||
        executor_create_count != 1 || executor_resume_count != 2 ||
        executor_suspend_count != 1 || executor_terminate_count != 1 ||
        executor_instance_destroy_count != 0 ||
        executor_program_destroy_count != 0)
        fail("executor active lifecycle");
    cb_kernel_destroy(kernel);
    if (executor_instance_destroy_count != 1 ||
        executor_program_destroy_count != 1)
        fail("executor destroy lifecycle");
}

static int pidcheck_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    char cwd[CB_PATH_MAX];
    const char *exec_value;
    int expected_pid;
    int closed_fd;
    int retained_fd;
    struct cb_stat_v1 stat_buffer;
    (void)envp;
    if (argc != 4)
        return 20;
    expected_pid = atoi(argv[1]);
    closed_fd = atoi(argv[2]);
    retained_fd = atoi(argv[3]);
    if (api->getpid() != expected_pid)
        return 21;
    if (api->fstat(closed_fd, &stat_buffer) == 0 ||
        api->get_errno() != CB_EBADF)
        return 22;
    if (api->fstat(retained_fd, &stat_buffer) < 0 ||
        stat_buffer.type != CB_NODE_REGULAR)
        return 23;
    if (api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/tmp") != 0)
        return 24;
    exec_value = api->getenv("EXECVAR");
    if (exec_value == NULL || strcmp(exec_value, "after-exec") != 0 ||
        api->getenv("HOME") != NULL)
        return 25;
    if (*api->environ_location() == NULL ||
        (*api->environ_location())[0] == NULL ||
        strcmp((*api->environ_location())[0], "EXECVAR=after-exec") != 0 ||
        (*api->environ_location())[1] != NULL)
        return 26;
    if (api->getopt_state_location()->optind != 1 ||
        api->getopt_state_location()->opterr != 1 ||
        api->getopt_state_location()->optopt != 0 ||
        api->getopt_state_location()->optarg != NULL)
        return 27;
    return 7;
}

static int execprobe_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    char pid[32];
    char closed_descriptor[32];
    char retained_descriptor[32];
    char *next_argv[5];
    char *next_environment[] = {(char *)"EXECVAR=after-exec", NULL};
    int closed_fd;
    int retained_fd;
    (void)argc;
    (void)argv;
    (void)envp;
    closed_fd = api->open("/tmp/cloexec", CB_O_WRONLY | CB_O_CREAT, 0600);
    retained_fd = api->open("/tmp/retained", CB_O_WRONLY | CB_O_CREAT, 0600);
    if (closed_fd < 0 || retained_fd < 0 ||
        api->set_cloexec(closed_fd, 1) < 0 ||
        api->dup2(closed_fd, closed_fd) != closed_fd ||
        api->chdir("/tmp") < 0 ||
        api->setenv("EXECVAR", "before-exec", 1) < 0)
        return 10;
    /* Mutate this task's getopt state away from its defaults so the exec
       below can be shown to reset it rather than leaking it forward. */
    api->getopt_state_location()->optind = 5;
    api->getopt_state_location()->opterr = 0;
    api->getopt_state_location()->optopt = (int)'q';
    api->getopt_state_location()->optarg = pid;
    snprintf(pid, sizeof(pid), "%d", (int)api->getpid());
    snprintf(closed_descriptor, sizeof(closed_descriptor), "%d", closed_fd);
    snprintf(retained_descriptor, sizeof(retained_descriptor), "%d",
             retained_fd);
    next_argv[0] = (char *)"pidcheck";
    next_argv[1] = pid;
    next_argv[2] = closed_descriptor;
    next_argv[3] = retained_descriptor;
    next_argv[4] = NULL;
    if (api->exec("pidcheck", next_argv, next_environment) < 0)
        return 11;
    return 12;
}

static int unlinkprobe_main(const struct cb_api_v1 *api, int argc,
                            char *const argv[], char *const envp[])
{
    static const char payload[] = "still here";
    struct cb_stat_v1 stat_buffer;
    char buffer[sizeof(payload)];
    int fd;
    (void)argc;
    (void)argv;
    (void)envp;

    fd = api->open("/tmp/unlinked", CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC,
                   0600);
    if (fd < 0)
        return 30;
    if (api->write(fd, payload, sizeof(payload)) != (cb_ssize_t)sizeof(payload))
        return 31;
    if (api->unlink("/tmp/unlinked") < 0)
        return 32;
    if (api->stat("/tmp/unlinked", &stat_buffer) == 0 ||
        api->get_errno() != CB_ENOENT)
        return 33;
    if (api->lseek(fd, 0, CB_SEEK_SET) != 0)
        return 34;
    memset(buffer, 0, sizeof(buffer));
    if (api->read(fd, buffer, sizeof(buffer)) != (cb_ssize_t)sizeof(buffer))
        return 35;
    if (memcmp(buffer, payload, sizeof(payload)) != 0)
        return 36;
    if (api->fstat(fd, &stat_buffer) < 0 ||
        stat_buffer.size != sizeof(payload))
        return 37;
    if (api->close(fd) < 0)
        return 38;
    return 0;
}

static int pipeallocprobe_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    int held[60];
    int fail_at;
    int index;
    (void)argc;
    (void)argv;
    (void)envp;

    for (fail_at = 0; fail_at < 3; ++fail_at) {
        int descriptors[2] = {-71, -72};
        allocation_failure_countdown = fail_at;
        if (api->pipe(descriptors) == 0) {
            api->close(descriptors[0]);
            api->close(descriptors[1]);
            return 40 + fail_at;
        }
        allocation_failure_countdown = -1;
        if (api->get_errno() != CB_ENOMEM)
            return 43 + fail_at;
        if (descriptors[0] != -71 || descriptors[1] != -72)
            return 46 + fail_at;
    }

    for (index = 0; index < 60; ++index) {
        held[index] = api->dup(0);
        if (held[index] != index + 3)
            return 50;
    }
    {
        int descriptors[2] = {-71, -72};
        if (api->pipe(descriptors) == 0 || api->get_errno() != CB_EMFILE)
            return 51;
        if (descriptors[0] != -71 || descriptors[1] != -72)
            return 52;
        if (api->dup(0) != 63)
            return 53;
        if (api->close(63) < 0)
            return 54;
    }
    for (index = 0; index < 60; ++index) {
        if (api->close(held[index]) < 0)
            return 55;
    }
    return 0;
}

static int pipezeropeer_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    pipe_zero_peer_ran = 1;
    return 0;
}

static int pipezeroprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    char *peer_argv[] = {(char *)"pipezeropeer", NULL};
    char byte = 'x';
    cb_pid_t peer;
    int descriptors[2];
    int status;
    (void)argc;
    (void)argv;

    pipe_zero_peer_ran = 0;
    if (api->pipe(descriptors) < 0)
        return 60;
    if (api->spawn("pipezeropeer", peer_argv, envp, NULL, 0, &peer) < 0)
        return 61;
    if (api->write(descriptors[1], &byte, 0) != 0)
        return 62;
    if (pipe_zero_peer_ran)
        return 63;
    if (api->close(descriptors[1]) < 0)
        return 64;
    if (api->read(descriptors[0], &byte, 0) != 0)
        return 65;
    if (pipe_zero_peer_ran)
        return 66;
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 67;
    if (!pipe_zero_peer_ran)
        return 68;
    if (api->close(descriptors[0]) < 0)
        return 69;
    return 0;
}

static int pipeedgepeer_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    char byte;
    cb_ssize_t count;
    (void)argc;
    (void)argv;
    (void)envp;
    pipe_edge_peer_state = 1;
    count = api->read(pipe_edge_read_fd, &byte, 1);
    if (count != 0)
        return 70;
    if (cb_test_current_descriptor_poll(pipe_edge_read_fd, CB_POLL_READ) !=
        (CB_POLL_READ | CB_POLL_HUP))
        return 201;
    if (cb_test_current_wake_reason() != CB_WAKE_PIPE_CHANGED)
        return 82;
    pipe_edge_peer_state = 2;
    return 0;
}

static int pipeedgeprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    char *peer_argv[] = {(char *)"pipeedgepeer", NULL};
    struct cb_spawn_action_v1 close_writer;
    char byte = 'x';
    cb_pid_t peer;
    int descriptors[2];
    int status;
    (void)argc;
    (void)argv;

    if (api->pipe(descriptors) < 0)
        return 71;
    if (cb_test_current_descriptor_poll(descriptors[0], CB_POLL_READ) != 0 ||
        cb_test_current_descriptor_poll(descriptors[1], CB_POLL_WRITE) !=
            CB_POLL_WRITE)
        return 196;
    if (api->close(descriptors[0]) < 0)
        return 72;
    if (cb_test_current_descriptor_poll(descriptors[1], CB_POLL_WRITE) !=
        (CB_POLL_WRITE | CB_POLL_ERR))
        return 197;
    if (api->write(descriptors[1], &byte, 1) != -1 ||
        api->get_errno() != CB_EPIPE)
        return 73;
    if (api->close(descriptors[1]) < 0)
        return 74;

    if (api->pipe(descriptors) < 0)
        return 75;
    pipe_edge_read_fd = descriptors[0];
    pipe_edge_peer_state = 0;
    close_writer.abi_version = CB_ABI_VERSION_V1;
    close_writer.struct_size = sizeof(close_writer);
    close_writer.type = CB_SPAWN_CLOSE;
    close_writer.from_fd = descriptors[1];
    close_writer.to_fd = -1;
    if (api->spawn("pipeedgepeer", peer_argv, envp, &close_writer, 1,
                   &peer) < 0)
        return 76;
    if (api->close(descriptors[0]) < 0)
        return 77;
    api->yield();
    if (pipe_edge_peer_state != 1)
        return 78;
    if (api->close(descriptors[1]) < 0)
        return 79;
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 80;
    if (pipe_edge_peer_state != 2)
        return 81;
    return 0;
}

static unsigned char pipe_pattern(size_t offset)
{
    return (unsigned char)((offset * 37U + 11U) & 0xffU);
}

static int pipecapacitypeer_main(const struct cb_api_v1 *api, int argc,
                                 char *const argv[], char *const envp[])
{
    unsigned char buffer[777];
    size_t total = 0;
    (void)argc;
    (void)argv;
    (void)envp;
    pipe_capacity_peer_started = 1;
    for (;;) {
        cb_ssize_t count = api->read(pipe_capacity_read_fd, buffer,
                                     sizeof(buffer));
        size_t index;
        if (count < 0)
            return 90;
        if (count == 0)
            break;
        for (index = 0; index < (size_t)count; ++index) {
            if (buffer[index] != pipe_pattern(total + index))
                return 91;
        }
        total += (size_t)count;
    }
    pipe_capacity_bytes_read = total;
    return total == 10000 ? 0 : 92;
}

static int pipecapacityprobe_main(const struct cb_api_v1 *api, int argc,
                                  char *const argv[], char *const envp[])
{
    unsigned char payload[10000];
    char *peer_argv[] = {(char *)"pipecapacitypeer", NULL};
    struct cb_spawn_action_v1 close_writer;
    cb_pid_t peer;
    int descriptors[2];
    int status;
    size_t index;
    (void)argc;
    (void)argv;

    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = pipe_pattern(index);
    if (api->pipe(descriptors) < 0)
        return 93;
    pipe_capacity_read_fd = descriptors[0];
    pipe_capacity_peer_started = 0;
    pipe_capacity_bytes_read = 0;
    close_writer.abi_version = CB_ABI_VERSION_V1;
    close_writer.struct_size = sizeof(close_writer);
    close_writer.type = CB_SPAWN_CLOSE;
    close_writer.from_fd = descriptors[1];
    close_writer.to_fd = -1;
    if (api->spawn("pipecapacitypeer", peer_argv, envp, &close_writer, 1,
                   &peer) < 0)
        return 94;
    if (api->close(descriptors[0]) < 0)
        return 95;
    if (pipe_capacity_peer_started)
        return 96;
    if (api->write(descriptors[1], payload, sizeof(payload)) !=
        (cb_ssize_t)sizeof(payload))
        return 97;
    if (!pipe_capacity_peer_started)
        return 98;
    if (api->close(descriptors[1]) < 0)
        return 99;
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 100;
    if (pipe_capacity_bytes_read != sizeof(payload))
        return 101;
    return 0;
}

static int environpeer_main(const struct cb_api_v1 *api, int argc,
                            char *const argv[], char *const envp[])
{
    unsigned char buffer[777];
    size_t total = 0;
    const char *token;
    (void)argc;
    (void)argv;
    (void)envp;

    if (*api->environ_location() == NULL)
        return 260;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "parent-before-block") != 0 ||
        api->getenv("PEERONLY") != NULL)
        return 261;
    if (api->setenv("TOKEN", "peer-environment", 1) < 0 ||
        api->setenv("PEERONLY", "yes", 1) < 0)
        return 262;
    environ_peer_started = 1;
    for (;;) {
        cb_ssize_t count = api->read(environ_pipe_read_fd, buffer,
                                     sizeof(buffer));
        size_t index;
        if (count < 0)
            return 263;
        if (count == 0)
            break;
        for (index = 0; index < (size_t)count; ++index) {
            if (buffer[index] != pipe_pattern(total + index))
                return 264;
        }
        total += (size_t)count;
    }
    environ_peer_bytes_read = total;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "peer-environment") != 0 ||
        api->getenv("PEERONLY") == NULL)
        return 265;
    return total == 10000 ? 0 : 266;
}

static int environprobe_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    unsigned char payload[10000];
    char *peer_argv[] = {(char *)"environpeer", NULL};
    struct cb_spawn_action_v1 close_writer;
    char ***location;
    char **environment_before;
    cb_pid_t peer;
    int descriptors[2];
    int status;
    size_t index;
    const char *token;
    (void)argc;
    (void)argv;
    (void)envp;

    location = api->environ_location();
    if (location == NULL || *location == NULL)
        return 270;
    if (api->setenv("TOKEN", "parent-before-block", 1) < 0 ||
        api->unsetenv("PEERONLY") < 0)
        return 271;
    environment_before = *location;

    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = pipe_pattern(index);
    if (api->pipe(descriptors) < 0)
        return 272;
    environ_pipe_read_fd = descriptors[0];
    environ_peer_started = 0;
    environ_peer_bytes_read = 0;
    close_writer.abi_version = CB_ABI_VERSION_V1;
    close_writer.struct_size = sizeof(close_writer);
    close_writer.type = CB_SPAWN_CLOSE;
    close_writer.from_fd = descriptors[1];
    close_writer.to_fd = -1;
    /* Passing NULL (not this task's own stale startup envp) makes the peer
       inherit this task's *current* environment, as mutated above. */
    if (api->spawn("environpeer", peer_argv, NULL, &close_writer, 1,
                   &peer) < 0)
        return 273;
    if (api->close(descriptors[0]) < 0)
        return 274;
    if (environ_peer_started)
        return 275;
    /* Force this task to block mid-write so the peer runs and mutates its
       own environment before this task resumes. */
    if (api->write(descriptors[1], payload, sizeof(payload)) !=
        (cb_ssize_t)sizeof(payload))
        return 276;
    if (!environ_peer_started)
        return 277;
    if (api->close(descriptors[1]) < 0)
        return 278;

    /* The peer's mutations must not have leaked into this task's vector. */
    if (*location != environment_before)
        return 279;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "parent-before-block") != 0 ||
        api->getenv("PEERONLY") != NULL)
        return 280;

    if (api->waitpid(peer, &status) != peer || status != 0)
        return 281;
    if (environ_peer_bytes_read != sizeof(payload))
        return 282;

    /* setenv/unsetenv after the peer exited are still reflected locally,
       through the same live vector this task observed all along. */
    if (api->setenv("TOKEN", "parent-after-wait", 1) < 0)
        return 283;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "parent-after-wait") != 0)
        return 284;
    return 0;
}

static int exitwaitprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    char fd_argument[32];
    char *child_argv[3];
    unsigned char observed[4];
    cb_ssize_t total = 0;
    int descriptors[2];
    cb_pid_t child;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->pipe(descriptors) < 0)
        return 290;
    snprintf(fd_argument, sizeof(fd_argument), "%d", descriptors[1]);
    child_argv[0] = (char *)"libcexitprobe";
    child_argv[1] = fd_argument;
    child_argv[2] = NULL;
    /* The child inherits descriptors[1] directly and must never close it
       itself; only cb_libc_exit's automatic reclamation may close it. */
    if (api->spawn("libcexitprobe", child_argv, NULL, NULL, 0, &child) < 0)
        return 291;
    if (api->close(descriptors[1]) < 0)
        return 292;
    for (;;) {
        cb_ssize_t count = api->read(descriptors[0], observed + total,
                                     sizeof(observed) - (size_t)total);
        if (count < 0)
            return 293;
        if (count == 0)
            break;
        total += count;
        if ((size_t)total >= sizeof(observed))
            return 294;
    }
    /* Only the write before exit() must have landed: proves the write after
       exit(7) never executed (no fallthrough). */
    if (total != 1 || observed[0] != 'A')
        return 295;
    /* The child is a zombie but not yet reaped: its heap must already be
       reclaimed by cb_libc_exit's underlying task cleanup. */
    if (cb_test_task_allocation_count(child) != 0)
        return 296;
    if (api->waitpid(child, &status) != child || status != 7)
        return 297;
    if (api->close(descriptors[0]) < 0)
        return 298;
    return 0;
}

static int getoptwaitprobe_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    char report_a_fd[32];
    char report_b_fd[32];
    char sync_write_fd[32];
    char sync_read_fd[32];
    char *argv_a[6];
    char *argv_b[8];
    int report_a[2];
    int report_b[2];
    int sync_pipe[2];
    struct cb_spawn_action_v1 close_for_a[4];
    struct cb_spawn_action_v1 close_for_b[4];
    cb_pid_t child_a;
    cb_pid_t child_b;
    int status;
    size_t index;
    unsigned char results[2];
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->pipe(report_a) < 0 || api->pipe(report_b) < 0 ||
        api->pipe(sync_pipe) < 0)
        return 300;
    snprintf(report_a_fd, sizeof(report_a_fd), "%d", report_a[1]);
    snprintf(report_b_fd, sizeof(report_b_fd), "%d", report_b[1]);
    snprintf(sync_write_fd, sizeof(sync_write_fd), "%d", sync_pipe[1]);
    snprintf(sync_read_fd, sizeof(sync_read_fd), "%d", sync_pipe[0]);

    /* Spawn copies this task's entire descriptor table, so without these,
       each child would also inherit the OTHER child's pipe ends -- in
       particular role B would inherit its own peer's sync-pipe write end,
       so its own drain loop could never see EOF (a real deadlock this
       caught on the first run). Each child keeps only the two descriptors
       named in its own argv. */
    close_for_a[0].from_fd = report_a[0];
    close_for_a[1].from_fd = report_b[0];
    close_for_a[2].from_fd = report_b[1];
    close_for_a[3].from_fd = sync_pipe[0];
    close_for_b[0].from_fd = report_a[0];
    close_for_b[1].from_fd = report_a[1];
    close_for_b[2].from_fd = report_b[0];
    close_for_b[3].from_fd = sync_pipe[1];
    for (index = 0; index < 4; ++index) {
        close_for_a[index].abi_version = CB_ABI_VERSION_V1;
        close_for_a[index].struct_size = sizeof(close_for_a[index]);
        close_for_a[index].type = CB_SPAWN_CLOSE;
        close_for_a[index].to_fd = -1;
        close_for_b[index].abi_version = CB_ABI_VERSION_V1;
        close_for_b[index].struct_size = sizeof(close_for_b[index]);
        close_for_b[index].type = CB_SPAWN_CLOSE;
        close_for_b[index].to_fd = -1;
    }

    argv_a[0] = (char *)"libcgetoptprobe";
    argv_a[1] = (char *)"A";
    argv_a[2] = report_a_fd;
    argv_a[3] = sync_write_fd;
    argv_a[4] = (char *)"plain";
    argv_a[5] = NULL;
    if (api->spawn("libcgetoptprobe", argv_a, NULL, close_for_a, 4,
                   &child_a) < 0)
        return 301;

    argv_b[0] = (char *)"libcgetoptprobe";
    argv_b[1] = (char *)"B";
    argv_b[2] = report_b_fd;
    argv_b[3] = sync_read_fd;
    argv_b[4] = (char *)"-x";
    argv_b[5] = (char *)"--";
    argv_b[6] = (char *)"z";
    argv_b[7] = NULL;
    if (api->spawn("libcgetoptprobe", argv_b, NULL, close_for_b, 4,
                   &child_b) < 0)
        return 302;

    /* Close this task's own copies; only the children's inherited copies
       must remain, so pipe EOF and blocking behave as intended below. */
    if (api->close(report_a[1]) < 0 || api->close(report_b[1]) < 0 ||
        api->close(sync_pipe[0]) < 0 || api->close(sync_pipe[1]) < 0)
        return 303;

    if (api->waitpid(child_a, &status) != child_a || status != 0)
        return 304;
    if (api->read(report_a[0], results, sizeof(results)) != 2)
        return 305;
    /* Both of role A's "no options" checks (before and after role B ran
       its own getopt() calls in between) must have observed optind == 1:
       role B's task-local state never leaked into role A's. */
    if (results[0] != '1' || results[1] != '1')
        return 306;
    if (api->close(report_a[0]) < 0)
        return 307;

    if (api->waitpid(child_b, &status) != child_b || status != 0)
        return 308;
    if (api->read(report_b[0], results, sizeof(results)) != 2)
        return 309;
    /* Role B's unknown-option and "--" checks. */
    if (results[0] != '1' || results[1] != '1')
        return 310;
    if (api->close(report_b[0]) < 0)
        return 311;
    /* Role B's unknown-option hit hit ran with the default opterr == 1:
       a real diagnostic must have reached stderr. */
    if (strcmp(captured_streams[2], "libcgetoptprobe: illegal option -- x\n") != 0)
        return 312;
    return 0;
}

static int getopterrprobe_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    char report_fd[32];
    char *probe_argv[6];
    int report[2];
    cb_pid_t child;
    int status;
    unsigned char result;
    size_t before;
    (void)argc;
    (void)argv;
    (void)envp;

    before = captured_stream_sizes[2];
    if (api->pipe(report) < 0)
        return 320;
    snprintf(report_fd, sizeof(report_fd), "%d", report[1]);
    probe_argv[0] = (char *)"libcgetoptprobe";
    probe_argv[1] = (char *)"E";
    probe_argv[2] = report_fd;
    probe_argv[3] = (char *)"0";
    probe_argv[4] = (char *)"-z";
    probe_argv[5] = NULL;
    if (api->spawn("libcgetoptprobe", probe_argv, NULL, NULL, 0, &child) < 0)
        return 321;
    if (api->close(report[1]) < 0)
        return 322;
    if (api->waitpid(child, &status) != child || status != 0)
        return 323;
    if (api->read(report[0], &result, 1) != 1 || result != '1')
        return 324;
    if (api->close(report[0]) < 0)
        return 325;
    /* opterr == 0 must suppress the diagnostic entirely: nothing new on
       stderr since before this task ran. */
    if (captured_stream_sizes[2] != before)
        return 326;
    return 0;
}

static int getoptclusterprobe_main(const struct cb_api_v1 *api, int argc,
                                   char *const argv[], char *const envp[])
{
    char report_c_fd[32];
    char report_d_fd[32];
    char sync_write_fd[32];
    char sync_read_fd[32];
    char *argv_c[6];
    char *argv_d[7];
    int report_c[2];
    int report_d[2];
    int sync_pipe[2];
    struct cb_spawn_action_v1 close_for_c[4];
    struct cb_spawn_action_v1 close_for_d[4];
    cb_pid_t child_c;
    cb_pid_t child_d;
    int status;
    size_t index;
    unsigned char results[2];
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->pipe(report_c) < 0 || api->pipe(report_d) < 0 ||
        api->pipe(sync_pipe) < 0)
        return 330;
    snprintf(report_c_fd, sizeof(report_c_fd), "%d", report_c[1]);
    snprintf(report_d_fd, sizeof(report_d_fd), "%d", report_d[1]);
    snprintf(sync_write_fd, sizeof(sync_write_fd), "%d", sync_pipe[1]);
    snprintf(sync_read_fd, sizeof(sync_read_fd), "%d", sync_pipe[0]);

    close_for_c[0].from_fd = report_c[0];
    close_for_c[1].from_fd = report_d[0];
    close_for_c[2].from_fd = report_d[1];
    close_for_c[3].from_fd = sync_pipe[0];
    close_for_d[0].from_fd = report_c[0];
    close_for_d[1].from_fd = report_c[1];
    close_for_d[2].from_fd = report_d[0];
    close_for_d[3].from_fd = sync_pipe[1];
    for (index = 0; index < 4; ++index) {
        close_for_c[index].abi_version = CB_ABI_VERSION_V1;
        close_for_c[index].struct_size = sizeof(close_for_c[index]);
        close_for_c[index].type = CB_SPAWN_CLOSE;
        close_for_c[index].to_fd = -1;
        close_for_d[index].abi_version = CB_ABI_VERSION_V1;
        close_for_d[index].struct_size = sizeof(close_for_d[index]);
        close_for_d[index].type = CB_SPAWN_CLOSE;
        close_for_d[index].to_fd = -1;
    }

    argv_c[0] = (char *)"libcgetoptprobe";
    argv_c[1] = (char *)"C";
    argv_c[2] = report_c_fd;
    argv_c[3] = sync_write_fd;
    argv_c[4] = (char *)"-xy";
    argv_c[5] = NULL;
    if (api->spawn("libcgetoptprobe", argv_c, NULL, close_for_c, 4,
                   &child_c) < 0)
        return 331;

    argv_d[0] = (char *)"libcgetoptprobe";
    argv_d[1] = (char *)"D";
    argv_d[2] = report_d_fd;
    argv_d[3] = sync_read_fd;
    argv_d[4] = (char *)"-a";
    argv_d[5] = (char *)"-b";
    argv_d[6] = NULL;
    if (api->spawn("libcgetoptprobe", argv_d, NULL, close_for_d, 4,
                   &child_d) < 0)
        return 332;

    if (api->close(report_c[1]) < 0 || api->close(report_d[1]) < 0 ||
        api->close(sync_pipe[0]) < 0 || api->close(sync_pipe[1]) < 0)
        return 333;

    if (api->waitpid(child_c, &status) != child_c || status != 0)
        return 334;
    if (api->read(report_c[0], results, sizeof(results)) != 2)
        return 335;
    /* The mid-cluster scan cursor ('x' then 'y' from the same "-xy" argv
       element) must have survived role D's own unrelated getopt() calls
       running in between, unmodified. */
    if (results[0] != '1' || results[1] != '1')
        return 336;
    if (api->close(report_c[0]) < 0)
        return 337;

    if (api->waitpid(child_d, &status) != child_d || status != 0)
        return 338;
    if (api->read(report_d[0], results, sizeof(results)) != 2)
        return 339;
    if (results[0] != '1' || results[1] != '1')
        return 340;
    if (api->close(report_d[0]) < 0)
        return 341;
    return 0;
}

static int errxprobe_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    char sync_write_fd[32];
    char sync_read_fd[32];
    char *argv_a[4];
    char *argv_b[4];
    int sync_pipe[2];
    struct cb_spawn_action_v1 close_for_a;
    struct cb_spawn_action_v1 close_for_b;
    cb_pid_t child_a;
    cb_pid_t child_b;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->pipe(sync_pipe) < 0)
        return 350;
    snprintf(sync_write_fd, sizeof(sync_write_fd), "%d", sync_pipe[1]);
    snprintf(sync_read_fd, sizeof(sync_read_fd), "%d", sync_pipe[0]);

    close_for_a.abi_version = CB_ABI_VERSION_V1;
    close_for_a.struct_size = sizeof(close_for_a);
    close_for_a.type = CB_SPAWN_CLOSE;
    close_for_a.from_fd = sync_pipe[0];
    close_for_a.to_fd = -1;
    close_for_b = close_for_a;
    close_for_b.from_fd = sync_pipe[1];

    /* Two tasks, two distinct argv[0] program identities, spawned under
       the SAME registered command name -- proving getprogname() tracks
       this task's own argv[0], not the registry lookup key. */
    argv_a[0] = (char *)"errx-task-alpha";
    argv_a[1] = (char *)"A";
    argv_a[2] = sync_write_fd;
    argv_a[3] = NULL;
    if (api->spawn("libcerrxprobe", argv_a, NULL, &close_for_a, 1,
                   &child_a) < 0)
        return 351;

    argv_b[0] = (char *)"errx-task-beta";
    argv_b[1] = (char *)"B";
    argv_b[2] = sync_read_fd;
    argv_b[3] = NULL;
    if (api->spawn("libcerrxprobe", argv_b, NULL, &close_for_b, 1,
                   &child_b) < 0)
        return 352;

    if (api->close(sync_pipe[0]) < 0 || api->close(sync_pipe[1]) < 0)
        return 353;

    if (api->waitpid(child_a, &status) != child_a || status != 1)
        return 354;
    if (api->waitpid(child_b, &status) != child_b || status != 1)
        return 355;

    /* Each task's diagnostic must carry its own program identity, order-
       independent of exactly how the two tasks interleaved. */
    if (strstr(captured_streams[2], "errx-task-alpha: boom\n") == NULL ||
        strstr(captured_streams[2], "errx-task-beta: boom\n") == NULL)
        return 356;
    /* errx must never write to stdout. */
    if (captured_streams[1][0] != '\0')
        return 357;
    /* Already verified above, order-independently; clear the transcript so
       the run_case() caller's own blanket comparison isn't coupled to
       exactly how these two tasks happened to interleave. */
    captured_size = 0;
    captured[0] = '\0';
    captured_stream_sizes[1] = 0;
    captured_streams[1][0] = '\0';
    captured_stream_sizes[2] = 0;
    captured_streams[2][0] = '\0';
    return 0;
}

/* One shared test API keeps the binding alive across both real task stacks. */
static struct cb_api_v1 err_test_api;
static const struct cb_api_v1 *err_delegate;
static cb_pid_t err_children[2], err_last_writer;
static char err_diagnostics[2][128];
static size_t err_lengths[2];
static unsigned err_switches;

static cb_ssize_t err_short_write(int descriptor, const void *buffer, size_t count)
{
    cb_pid_t pid = err_delegate->getpid();
    size_t index = pid == err_children[0] ? 0 : 1;
    cb_ssize_t result;
    if (descriptor != 2 || pid != err_children[index])
        fail("err wrote outside the child's stderr");
    if (count > 2)
        count = 2;
    result = err_delegate->write(descriptor, buffer, count);
    if (result <= 0 || (size_t)result >= sizeof(err_diagnostics[index]) - err_lengths[index])
        fail("err short-write test capture");
    memcpy(err_diagnostics[index] + err_lengths[index], buffer, (size_t)result);
    err_lengths[index] += (size_t)result;
    err_diagnostics[index][err_lengths[index]] = '\0';
    if (err_last_writer != 0 && err_last_writer != pid)
        ++err_switches;
    err_last_writer = pid;
    /* Successful I/O is allowed to change errno. Do so before actually
       yielding to the other ordinary-source diagnostic mid-message. */
    err_delegate->set_errno(CB_EIO);
    err_delegate->yield();
    return result;
}

static int err_short_start(const struct cb_api_v1 *api, int argc,
                            char *const argv[], char *const envp[])
{
    (void)api;
    (void)envp;
    return cb_libc_start(&err_test_api, argc, argv, cb_err_probe_main);
}

static const struct cb_program_v1 err_short_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "errshort", 0,
    64 * 1024, err_short_start
};

static int err_interleave_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    char *alpha[] = {(char *)"err-alpha", NULL};
    char *beta[] = {(char *)"err-beta", (char *)"beta", NULL};
    int status;
    (void)argc;
    (void)argv;
    (void)envp;
    err_delegate = api;
    err_test_api = *api;
    err_test_api.write = err_short_write;
    memset(err_diagnostics, 0, sizeof(err_diagnostics));
    memset(err_lengths, 0, sizeof(err_lengths));
    err_last_writer = 0;
    err_switches = 0;
    if (api->spawn("errshort", alpha, NULL, NULL, 0, &err_children[0]) < 0 ||
        api->spawn("errshort", beta, NULL, NULL, 0, &err_children[1]) < 0)
        return 401;
    if (api->waitpid(err_children[0], &status) != err_children[0] || status != 7 ||
        api->waitpid(err_children[1], &status) != err_children[1] || status != 8)
        return 402;
    if (err_switches < 2 ||
        strcmp(err_diagnostics[0], "err-alpha: path alpha %: no such file or directory\n") != 0 ||
        strcmp(err_diagnostics[1], "err-beta: beta: bad file descriptor\n") != 0)
        return 403;
    if (captured_stream_sizes[1] != 0)
        return 404;
    /* Each actual stderr byte was checked above per originating task; the
       merged stream's order is deliberately controlled by the scheduler. */
    captured_size = 0;
    captured[0] = '\0';
    captured_stream_sizes[2] = 0;
    captured_streams[2][0] = '\0';
    return 0;
}

static const struct cb_program_v1 err_interleave_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "errinterleave", 0,
    64 * 1024, err_interleave_main
};

static int direntbasicprobe_main(const struct cb_api_v1 *api, int argc,
                                 char *const argv[], char *const envp[])
{
    int handle;
    int handles[CB_MAX_DIRS];
    char name[CB_PATH_MAX];
    uint64_t inode;
    uint32_t type;
    int seen_bin = 0, seen_tmp = 0, seen_home = 0;
    int result;
    int fd;
    int index;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Root enumeration. */
    handle = api->opendir("/");
    if (handle < 0)
        return 360;
    for (;;) {
        result = api->readdir(handle, name, sizeof(name), &inode, &type);
        if (result < 0)
            return 361;
        if (name[0] == '\0')
            break;
        if (strcmp(name, "bin") == 0) {
            seen_bin = 1;
            if (type != CB_NODE_DIRECTORY)
                return 362;
        } else if (strcmp(name, "tmp") == 0) {
            seen_tmp = 1;
            if (type != CB_NODE_DIRECTORY)
                return 363;
        } else if (strcmp(name, "home") == 0) {
            seen_home = 1;
            if (type != CB_NODE_DIRECTORY)
                return 364;
        }
    }
    if (!seen_bin || !seen_tmp || !seen_home)
        return 365;
    /* Clean end of directory must not touch errno at all -- proved by
       priming a NONZERO sentinel first; testing from errno == 0 could
       not tell "preserved" apart from "cleared to 0". */
    api->set_errno(CB_EPERM);
    result = api->readdir(handle, name, sizeof(name), &inode, &type);
    if (result != 0 || name[0] != '\0' || api->get_errno() != CB_EPERM)
        return 366;
    api->set_errno(0);
    if (api->closedir(handle) < 0)
        return 367;

    /* opendir() on a regular file: ENOTDIR. */
    fd = api->open("/tmp/basicfile", CB_O_WRONLY | CB_O_CREAT, 0600);
    if (fd < 0 || api->close(fd) < 0)
        return 368;
    if (api->opendir("/tmp/basicfile") != -1 || api->get_errno() != CB_ENOTDIR)
        return 369;

    /* readdir()/closedir() on a bad descriptor: EBADF. */
    if (api->readdir(9999, name, sizeof(name), &inode, &type) != -1 ||
        api->get_errno() != CB_EBADF)
        return 370;
    if (api->closedir(9999) != -1 || api->get_errno() != CB_EBADF)
        return 371;

    /* A caller-supplied name buffer too small for the entry: ENAMETOOLONG,
       and the entry is NOT consumed -- a retry with a larger buffer must
       still observe it, rather than silently skipping past truncated
       data. */
    if (api->mkdir("/tmp/longnamedir", 0755) < 0)
        return 372;
    if ((fd = api->open("/tmp/longnamedir/longname",
                        CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 373;
    handle = api->opendir("/tmp/longnamedir");
    if (handle < 0)
        return 374;
    if (api->readdir(handle, name, 3, &inode, &type) != -1 ||
        api->get_errno() != CB_ENAMETOOLONG)
        return 375;
    if (api->readdir(handle, name, sizeof(name), &inode, &type) != 0 ||
        strcmp(name, "longname") != 0)
        return 376;
    if (api->closedir(handle) < 0)
        return 377;

    /* CB_MAX_DIRS exhaustion, then confirm a closed slot is reusable
       (indirect but real evidence that closedir() actually released its
       slot rather than leaking it). */
    for (index = 0; index < CB_MAX_DIRS; ++index) {
        handles[index] = api->opendir("/tmp");
        if (handles[index] < 0)
            return 380;
    }
    if (api->opendir("/tmp") != -1 || api->get_errno() != CB_EMFILE)
        return 381;
    if (api->closedir(handles[0]) < 0)
        return 382;
    handle = api->opendir("/tmp");
    if (handle < 0)
        return 383;
    if (api->closedir(handle) < 0)
        return 384;
    for (index = 1; index < CB_MAX_DIRS; ++index) {
        if (api->closedir(handles[index]) < 0)
            return 385;
    }
    return 0;
}

static int direntmutationprobe_main(const struct cb_api_v1 *api, int argc,
                                    char *const argv[], char *const envp[])
{
    int handle;
    char name[CB_PATH_MAX];
    uint64_t inode;
    uint32_t type;
    int fd;
    (void)argc;
    (void)argv;
    (void)envp;

    /* 4a, skip on removal: create C, B, A in that order so the resulting
       list (newest-first) is [A, B, C]. Read A, unlink the ALREADY-
       RETURNED A (not the next one), and confirm the following read
       skips B entirely and returns C -- unlinking the next not-yet-
       returned entry would only reflect ordinary shrinkage, not prove a
       skip. */
    if (api->mkdir("/tmp/skipdir", 0755) < 0)
        return 380;
    if ((fd = api->open("/tmp/skipdir/C", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 381;
    if ((fd = api->open("/tmp/skipdir/B", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 382;
    if ((fd = api->open("/tmp/skipdir/A", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 383;
    handle = api->opendir("/tmp/skipdir");
    if (handle < 0)
        return 384;
    if (api->readdir(handle, name, sizeof(name), &inode, &type) != 0 ||
        strcmp(name, "A") != 0)
        return 385;
    if (api->unlink("/tmp/skipdir/A") < 0)
        return 386;
    if (api->readdir(handle, name, sizeof(name), &inode, &type) != 0 ||
        strcmp(name, "C") != 0)
        return 387;
    if (api->closedir(handle) < 0)
        return 388;

    /* 4b, duplicate on insertion: create B, A in that order, so the list
       is [A, B]. Read A, then create X (prepended, giving [X, A, B]),
       and confirm the following read re-returns A rather than B or X. */
    if (api->mkdir("/tmp/dupdir", 0755) < 0)
        return 389;
    if ((fd = api->open("/tmp/dupdir/B", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 390;
    if ((fd = api->open("/tmp/dupdir/A", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 391;
    handle = api->opendir("/tmp/dupdir");
    if (handle < 0)
        return 392;
    if (api->readdir(handle, name, sizeof(name), &inode, &type) != 0 ||
        strcmp(name, "A") != 0)
        return 393;
    if ((fd = api->open("/tmp/dupdir/X", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 394;
    if (api->readdir(handle, name, sizeof(name), &inode, &type) != 0 ||
        strcmp(name, "A") != 0)
        return 395;
    if (api->closedir(handle) < 0)
        return 396;
    return 0;
}

static int direntisolationchild_main(const struct cb_api_v1 *api, int argc,
                                     char *const argv[], char *const envp[])
{
    int handle;
    char name[CB_PATH_MAX];
    uint64_t inode;
    uint32_t type;
    unsigned char count = 0;
    int result;
    int sync_fd;
    unsigned char payload[10000];
    unsigned char buffer[777];
    size_t index;
    (void)envp;
    if (argc != 3)
        return 397;
    sync_fd = atoi(argv[2]);

    handle = api->opendir("/tmp/isodir");
    if (handle < 0)
        return 398;
    result = api->readdir(handle, name, sizeof(name), &inode, &type);
    if (result != 0 || name[0] == '\0')
        return 399;
    ++count;

    if (argv[1][0] == 'A') {
        for (index = 0; index < sizeof(payload); ++index)
            payload[index] = (unsigned char)(index & 0xff);
        if (api->write(sync_fd, payload, sizeof(payload)) !=
            (cb_ssize_t)sizeof(payload))
            return 400;
    } else {
        for (;;) {
            cb_ssize_t got = api->read(sync_fd, buffer, sizeof(buffer));
            if (got < 0)
                return 401;
            if (got == 0)
                break;
        }
    }

    for (;;) {
        result = api->readdir(handle, name, sizeof(name), &inode, &type);
        if (result != 0)
            return 402;
        if (name[0] == '\0')
            break;
        ++count;
    }
    if (count != 3)
        return 403;
    if (api->closedir(handle) < 0)
        return 404;
    return 0;
}

static int direntisolationprobe_main(const struct cb_api_v1 *api, int argc,
                                     char *const argv[], char *const envp[])
{
    char sync_write_fd[32];
    char sync_read_fd[32];
    char *argv_a[4];
    char *argv_b[4];
    int sync_pipe[2];
    struct cb_spawn_action_v1 close_for_a;
    struct cb_spawn_action_v1 close_for_b;
    cb_pid_t child_a;
    cb_pid_t child_b;
    int status;
    int fd;
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->mkdir("/tmp/isodir", 0755) < 0)
        return 410;
    if ((fd = api->open("/tmp/isodir/one", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 411;
    if ((fd = api->open("/tmp/isodir/two", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 412;
    if ((fd = api->open("/tmp/isodir/three", CB_O_WRONLY | CB_O_CREAT, 0600)) < 0 ||
        api->close(fd) < 0)
        return 413;

    if (api->pipe(sync_pipe) < 0)
        return 414;
    snprintf(sync_write_fd, sizeof(sync_write_fd), "%d", sync_pipe[1]);
    snprintf(sync_read_fd, sizeof(sync_read_fd), "%d", sync_pipe[0]);

    close_for_a.abi_version = CB_ABI_VERSION_V1;
    close_for_a.struct_size = sizeof(close_for_a);
    close_for_a.type = CB_SPAWN_CLOSE;
    close_for_a.from_fd = sync_pipe[0];
    close_for_a.to_fd = -1;
    close_for_b = close_for_a;
    close_for_b.from_fd = sync_pipe[1];

    argv_a[0] = (char *)"direntisolationchild";
    argv_a[1] = (char *)"A";
    argv_a[2] = sync_write_fd;
    argv_a[3] = NULL;
    if (api->spawn("direntisolationchild", argv_a, NULL, &close_for_a, 1,
                   &child_a) < 0)
        return 415;

    argv_b[0] = (char *)"direntisolationchild";
    argv_b[1] = (char *)"B";
    argv_b[2] = sync_read_fd;
    argv_b[3] = NULL;
    if (api->spawn("direntisolationchild", argv_b, NULL, &close_for_b, 1,
                   &child_b) < 0)
        return 416;

    if (api->close(sync_pipe[0]) < 0 || api->close(sync_pipe[1]) < 0)
        return 417;

    if (api->waitpid(child_a, &status) != child_a || status != 0)
        return 418;
    if (api->waitpid(child_b, &status) != child_b || status != 0)
        return 419;
    return 0;
}

static int dirent_noop_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 0;
}

static int direntoldtableprobe_main(const struct cb_api_v1 *api, int argc,
                                    char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* This runs as a real, currently-scheduled task, so
       active_kernel->current is valid throughout -- unlike rebinding
       bound_api from outside any running task, which crashes the moment
       ordinary code touches errno/environ/anything else keyed off the
       current task. copy lives on THIS function's stack, so bound_api
       must be rebound back to the task's own stable `api` pointer before
       returning -- otherwise every later probe in this same process
       would dereference a dangling frame. */
    copy = *api;
    /* Everything through dirname_buffer_location is present; opendir/readdir/closedir
       are not -- exactly the "one release older" cb_api_v1 api_is_usable
       must still accept (see cb_libc.c's api_is_usable comment). */
    copy.struct_size = (uint32_t)offsetof(struct cb_api_v1, opendir);
    result = cb_libc_start(&copy, 0, NULL, cb_direntoldtable_main);
    cb_libc_start(api, 0, NULL, dirent_noop_main);
    if (result != 0)
        return 420;
    return 0;
}

static int direntopendirnulltableprobe_main(const struct cb_api_v1 *api,
                                            int argc, char *const argv[],
                                            char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Full struct_size, only opendir itself NULL. Distinct from
       direntoldtableprobe above (which shrinks struct_size): this catches
       a regression back to "trust struct_size alone" without also
       checking the field itself. Testing all three fields NULL together,
       exercised only through opendir(), would never actually prove
       readdir()/closedir() check their OWN fields -- see the two probes
       below, which is why this is now three separate scenarios instead
       of one. */
    copy = *api;
    copy.opendir = NULL;
    result = cb_libc_start(&copy, 0, NULL, cb_direntoldtable_main);
    cb_libc_start(api, 0, NULL, dirent_noop_main);
    if (result != 0)
        return 441;
    return 0;
}

static int direntreaddirnulltableprobe_main(const struct cb_api_v1 *api,
                                            int argc, char *const argv[],
                                            char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Only readdir NULL -- opendir and closedir are untouched and must
       still succeed. Each of cb_libc_opendir/readdir/closedir checks only
       its own field (not a bundled "all three or none" guard), so a table
       missing just readdir must degrade to ENOSYS for readdir() alone. */
    copy = *api;
    copy.readdir = NULL;
    result = cb_libc_start(&copy, 0, NULL, cb_direntreaddirunavail_main);
    cb_libc_start(api, 0, NULL, dirent_noop_main);
    if (result != 0)
        return 442;
    return 0;
}

static int direntclosedirnulltableprobe_main(const struct cb_api_v1 *api,
                                             int argc, char *const argv[],
                                             char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Only closedir NULL. Unlike the readdir case above, this must now
       make opendir() itself fail: cb_libc_opendir requires closedir to be
       usable before it ever acquires a descriptor, since closedir is the
       only thing that can release it (see cb_libc_opendir's own comment
       -- an independent review found the prior per-field-only guard let
       an allocation failure permanently leak the raw descriptor whenever
       closedir was absent). direntclosedirrebindprobe below is what
       actually exercises closedir()'s own per-call guard against an
       already-open handle. */
    copy = *api;
    copy.closedir = NULL;
    result = cb_libc_start(&copy, 0, NULL, cb_direntoldtable_main);
    cb_libc_start(api, 0, NULL, dirent_noop_main);
    if (result != 0)
        return 443;
    return 0;
}

static int direntclosedirrebindprobe_main(const struct cb_api_v1 *api,
                                          int argc, char *const argv[],
                                          char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Open and read under the full, working table first, so this really
       does acquire a real handle -- this is testing readdir()/closedir()'s
       own per-call guards against a handle that is ALREADY open, not
       opendir()'s closedir precondition (direntclosedirnulltableprobe
       above covers that). */
    if (cb_libc_start(api, 0, NULL, dirent_rebind_open) != 0)
        return 484;
    if (cb_libc_start(api, 0, NULL, dirent_rebind_readdir) != 0)
        return 485;

    /* Rebind to a copy with only closedir NULL, exercise closedir()'s own
       guard against the already-open handle, then restore the real table
       immediately -- the degraded copy has no way to ever close it. */
    copy = *api;
    copy.closedir = NULL;
    result = cb_libc_start(&copy, 0, NULL, dirent_rebind_closedir_reject);
    cb_libc_start(api, 0, NULL, dirent_noop_main);
    if (result != 0)
        return 486;

    if (cb_libc_start(api, 0, NULL, dirent_rebind_closedir_accept) != 0)
        return 487;
    return 0;
}

static int direntlibcallocfailprobe_main(const struct cb_api_v1 *api,
                                         int argc, char *const argv[],
                                         char *const envp[])
{
    int result;
    int handles[CB_MAX_DIRS];
    int index;
    int fail_at;
    (void)argc;
    (void)argv;
    (void)envp;

    /* api_allocate (behind bound_api->allocate) makes TWO underlying
       cb_allocate calls per logical allocation: one for the payload
       itself (the struct cb_libc_dir here) and one for its own
       bookkeeping node (the task-allocation-tracking entry that lets
       task_release_allocations find it later). Only testing fail_at == 0
       exercises "the payload allocation itself fails"; it never reaches
       the separate branch where the payload succeeds but the bookkeeping
       allocation fails and api_allocate must release the payload it had
       already acquired. Both must independently leave cb_libc_opendir's
       own acquired directory descriptor released, not leaked. */
    for (fail_at = 0; fail_at < 2; ++fail_at) {
        allocation_failure_countdown = fail_at;
        result = cb_libc_start(api, 0, NULL, cb_direntallocfail_main);
        cb_libc_start(api, 0, NULL, dirent_noop_main);
        allocation_failure_countdown = -1;
        if (result != 0)
            return 450 + fail_at;

        /* Real evidence the raw runtime descriptor cb_libc_opendir had
           already acquired was released, not leaked: every CB_MAX_DIRS
           slot must still be available from scratch. */
        for (index = 0; index < CB_MAX_DIRS; ++index) {
            handles[index] = api->opendir("/");
            if (handles[index] < 0)
                return 453 + fail_at;
        }
        for (index = 0; index < CB_MAX_DIRS; ++index) {
            if (api->closedir(handles[index]) < 0)
                return 456 + fail_at;
        }
    }
    return 0;
}

/* Shared by the reclaim probes below and by test_dir_reclaim_contract
   itself: reads "/tmp"'s RAMFS reference count via a throwaway whitebox
   task pointed at dir_reclaim_probe_kernel (set by the test driver before
   booting each scenario's kernel). The returned count always includes
   this call's own transient +1 retain (released again before returning),
   so a baseline of "nothing else holding it open" reads back as 2 (tree
   membership + this transient retain), not 1. */
static size_t dir_reclaim_check_tmp_references(void)
{
    struct cb_task probe_task;
    struct cb_vfs_node *tmp_node;
    size_t references;
    int probe_error = 0;
    memset(&probe_task, 0, sizeof(probe_task));
    probe_task.kernel = dir_reclaim_probe_kernel;
    probe_task.root = probe_task.cwd = dir_reclaim_probe_kernel->vfs_root;
    probe_task.error_cell = &probe_error;
    tmp_node = cb_vfs_opendir_path(&probe_task, "/tmp");
    if (tmp_node == NULL)
        return (size_t)-1;
    references = cb_test_ramfs_node_references(tmp_node);
    cb_vfs_node_release(tmp_node);
    return references;
}

static int direntreapexit_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    if (api->opendir("/tmp") < 0)
        return 1;
    /* Recorded from inside this still-running task, before it exits:
       proves the retain is genuinely held at this point, so the later
       checks (which must see it released) are testing a real transition,
       not a vacuous one where nothing was ever held in the first place. */
    dir_reclaim_exit_before_references = dir_reclaim_check_tmp_references();
    /* Deliberately not closed -- returning here triggers native_entry's
       automatic api->exit(status), and api_exit's dir_close_all must be
       the thing that releases this directory's retained node. */
    return 0;
}

static int direntreapexitboot_main(const struct cb_api_v1 *api, int argc,
                                   char *const argv[], char *const envp[])
{
    char *child_argv[] = {(char *)"direntreapexit", NULL};
    cb_pid_t child;
    int child_status;
    (void)argc;
    (void)argv;
    /* This program (not direntreapexit itself) is what the shell actually
       spawns and waits for: cb_kernel_boot always runs a command through
       "sh -c ...", and this shell always spawns a child and immediately
       blocks in waitpid() for it -- there is no bare-exec-replaces-shell
       path, and no background-job support to avoid that wait. If
       direntreapexit were booted directly, the shell's own blocking
       waitpid would reap it (running task_destroy, which also calls
       dir_close_all) the instant it exits, with no way to observe
       api_exit's own cleanup in isolation from that immediately-following
       reap. This orchestrator interposes exactly that missing window: it
       spawns direntreapexit itself, yields once (direntreapexit runs to
       completion -- open, self-check, exit -- entirely within that one
       turn, since it never blocks or yields itself), and only *then*
       checks state and waitpid()s to reap it. At the moment this resumes
       from yield(), direntreapexit is a zombie (api_exit has already run)
       but nothing has called waitpid() on it yet (this task is the only
       one that could -- the shell is still blocked waiting on THIS task,
       not on the grandchild) -- so this really does isolate api_exit's
       cleanup from task_destroy's. */
    if (api->spawn("direntreapexit", child_argv, envp, NULL, 0, &child) < 0)
        return 1;
    api->yield();
    dir_reclaim_exit_after_references = dir_reclaim_check_tmp_references();
    if (api->waitpid(child, &child_status) != child || child_status != 0)
        return 2;
    return 0;
}

static int direntreapexecpeer_main(const struct cb_api_v1 *api, int argc,
                                   char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    /* Recorded as the very first thing this program does after the exec
       transition, before it does anything else (including its own exit).
       If task_finish_exec's dir_close_all call were skipped, the old
       task's retain would still be sitting in directories[] at this exact
       point -- nothing else has run yet that could have released it. This
       is what actually distinguishes "task_finish_exec released it" from
       "it happened to get cleaned up later when this peer eventually
       exits anyway", which the driver's old post-run-only check could not
       tell apart. */
    dir_reclaim_exec_peer_references = dir_reclaim_check_tmp_references();
    return 0;
}

static int direntreapexec_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    char *peer_argv[] = {(char *)"direntreapexecpeer", NULL};
    (void)argc;
    (void)argv;
    if (api->opendir("/tmp") < 0)
        return 1;
    /* Deliberately not closed before exec -- directories have no
       close-on-exec concept; task_finish_exec's dir_close_all must close
       every one of them unconditionally. A successful exec never returns. */
    api->exec("direntreapexecpeer", peer_argv, envp);
    return 2;
}

static int direntreapdestroychild_main(const struct cb_api_v1 *api, int argc,
                                       char *const argv[], char *const envp[])
{
    int fds[2];
    char buffer[1];
    (void)argc;
    (void)argv;
    (void)envp;
    if (api->opendir("/tmp") < 0)
        return 1;
    if (api->pipe(fds) < 0)
        return 2;
    /* Both pipe ends belong to this task alone, so this blocks forever
       regardless of what the parent does, including exiting -- leaving
       this task still live and blocked, still holding the open directory,
       when the parent (PID1) exits and cb_kernel_run returns without ever
       reaping this orphan. cb_kernel_destroy must then call task_destroy
       on it directly. */
    api->read(fds[0], buffer, sizeof(buffer));
    return 3;
}

static int direntreapdestroyboot_main(const struct cb_api_v1 *api, int argc,
                                      char *const argv[], char *const envp[])
{
    char *child_argv[] = {(char *)"direntreapdestroychild", NULL};
    cb_pid_t child;
    (void)argc;
    (void)argv;
    if (api->spawn("direntreapdestroychild", child_argv, envp, NULL, 0,
                   &child) < 0)
        return 1;
    /* Give the child a turn to reach its blocking read before this task
       exits -- otherwise it never runs at all before boot_finished ends
       the scheduler loop, and the scenario would be vacuous (a task that
       never opened anything, not one genuinely holding a directory open).
       Deliberately no waitpid after this: the child is orphaned, still
       blocked, when this (PID1) task exits and cb_kernel_run returns. */
    api->yield();
    return 0;
}

static int terminalpeer_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    terminal_peer_ran = 1;
    console_poll_ready = 1;
    return 0;
}

static int terminalprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    static const unsigned char input[] = "z";
    char *peer_argv[] = {(char *)"terminalpeer", NULL};
    struct cb_stat_v1 stat_buffer;
    char byte = 'x';
    cb_pid_t peer;
    int descriptor;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    for (descriptor = 0; descriptor < 3; ++descriptor) {
        if (api->fstat(descriptor, &stat_buffer) < 0 ||
            stat_buffer.type != CB_NODE_TERMINAL || stat_buffer.mode != 0600)
            return 110 + descriptor;
    }
    console_poll_ready = 0;
    if (cb_test_current_descriptor_poll(0, CB_POLL_READ) != 0 ||
        cb_test_current_descriptor_poll(1, CB_POLL_READ | CB_POLL_WRITE) !=
            CB_POLL_WRITE ||
        cb_test_current_descriptor_poll(63, CB_POLL_READ) != -CB_EBADF)
        return 198;
    console_poll_ready = 1;
    if (cb_test_current_descriptor_poll(0, CB_POLL_READ) != CB_POLL_READ)
        return 199;
    if (api->read(1, &byte, 1) != -1 || api->get_errno() != CB_EBADF)
        return 113;
    if (api->write(0, &byte, 1) != -1 || api->get_errno() != CB_EBADF)
        return 114;
    console_poll_calls = 0;
    if (api->read(0, &byte, 0) != 0 || console_poll_calls != 0)
        return 115;
    console_read_error = 1;
    if (api->read(0, &byte, 1) != -1 || api->get_errno() != CB_EIO)
        return 116;
    console_read_error = 0;
    if (api->lseek(0, 0, CB_SEEK_SET) != -1 ||
        api->get_errno() != CB_ESPIPE)
        return 117;
    console_input = input;
    console_input_size = sizeof(input) - 1;
    console_input_position = 0;
    console_poll_ready = 0;
    console_poll_calls = 0;
    terminal_peer_ran = 0;
    if (api->spawn("terminalpeer", peer_argv, envp, NULL, 0, &peer) < 0)
        return 118;
    if (api->read(0, &byte, 1) != 1 || byte != 'z')
        return 119;
    if (!terminal_peer_ran || console_poll_calls == 0)
        return 120;
    if (cb_test_current_wake_reason() != CB_WAKE_CONSOLE_READY)
        return 158;
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 121;
    return 0;
}

static int descriptorchild_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    struct cb_stat_v1 stat_buffer;
    (void)argc;
    (void)argv;
    (void)envp;
    if (api->fstat(descriptor_child_fd, &stat_buffer) < 0 ||
        stat_buffer.type != CB_NODE_REGULAR)
        return 122;
    if (api->write(descriptor_child_fd, "d", 1) != 1)
        return 123;
    if (api->close(descriptor_child_fd) < 0)
        return 124;
    return 0;
}

static int descriptorprobe_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    char *child_argv[] = {(char *)"descriptorchild", NULL};
    struct cb_stat_v1 stat_buffer;
    char buffer[8];
    cb_pid_t child;
    int duplicate;
    int descriptor;
    int replacement;
    int status;
    (void)argc;
    (void)argv;

    descriptor = api->open("/tmp/shared", CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC,
                           0600);
    if (descriptor < 0 || api->write(descriptor, "ab", 2) != 2)
        return 125;
    if (cb_test_current_descriptor_poll(
            descriptor, CB_POLL_READ | CB_POLL_WRITE) !=
        (CB_POLL_READ | CB_POLL_WRITE))
        return 200;
    duplicate = api->dup(descriptor);
    if (duplicate < 0 || api->lseek(descriptor, 0, CB_SEEK_SET) != 0)
        return 126;
    if (api->read(duplicate, buffer, 1) != 1 || buffer[0] != 'a')
        return 127;
    if (api->read(descriptor, buffer, 1) != 1 || buffer[0] != 'b')
        return 128;

    replacement = api->open("/tmp/replaced",
                            CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC, 0600);
    if (replacement < 0 || api->write(replacement, "old", 3) != 3)
        return 129;
    if (api->set_cloexec(replacement, 1) < 0 ||
        api->dup2(descriptor, replacement) != replacement)
        return 130;
    if (api->write(replacement, "c", 1) != 1)
        return 131;
    descriptor_child_fd = replacement;
    if (api->spawn("descriptorchild", child_argv, envp, NULL, 0, &child) < 0)
        return 132;
    if (api->waitpid(child, &status) != child || status != 0)
        return 133;
    if (api->fstat(replacement, &stat_buffer) < 0 || stat_buffer.size != 4)
        return 134;
    if (api->lseek(duplicate, 0, CB_SEEK_SET) != 0 ||
        api->read(descriptor, buffer, 4) != 4 ||
        memcmp(buffer, "abcd", 4) != 0)
        return 135;
    if (api->close(duplicate) < 0 || api->close(replacement) < 0 ||
        api->close(descriptor) < 0)
        return 136;

    descriptor = api->open("/tmp/replaced", CB_O_RDONLY, 0);
    if (descriptor < 0 || api->read(descriptor, buffer, 3) != 3 ||
        memcmp(buffer, "old", 3) != 0 || api->close(descriptor) < 0)
        return 137;
    if (api->close(63) != -1 || api->get_errno() != CB_EBADF)
        return 138;
    if (api->dup(-1) != -1 || api->get_errno() != CB_EBADF)
        return 139;
    if (api->dup2(0, CB_MAX_FDS) != -1 || api->get_errno() != CB_EBADF)
        return 140;
    if (api->set_cloexec(63, 1) != -1 || api->get_errno() != CB_EBADF)
        return 141;
    return 0;
}

static int processchild_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    char cwd[CB_PATH_MAX];
    const char *token;
    (void)envp;
    if (argc != 2 || strcmp(argv[1], "original-argument") != 0)
        return 142;
    if (api->getppid() != process_expected_parent ||
        api->getpid() == process_expected_parent)
        return 143;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "original-environment") != 0)
        return 144;
    if (api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/tmp") != 0)
        return 145;
    if (api->setenv("TOKEN", "child-environment", 1) < 0 ||
        api->chdir("/home/user") < 0)
        return 146;
    process_child_phase = 1;
    api->yield();
    if (cb_test_current_wake_reason() != CB_WAKE_NONE)
        return 159;
    process_child_phase = 2;
    return 42;
}

static int processprobe_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    char argument[] = "original-argument";
    char *child_argv[] = {(char *)"processchild", argument, NULL};
    char cwd[CB_PATH_MAX];
    const char *token;
    cb_pid_t child;
    cb_pid_t second_child;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    if (api->getppid() != 1 || api->getpid() <= 1)
        return 147;
    process_expected_parent = api->getpid();
    if (api->chdir("/tmp") < 0 ||
        api->setenv("TOKEN", "original-environment", 1) < 0)
        return 148;
    process_child_phase = 0;
    if (api->spawn("processchild", child_argv, NULL, NULL, 0, &child) < 0 ||
        child == api->getpid())
        return 149;
    strcpy(argument, "mutated-argument!");
    if (api->setenv("TOKEN", "parent-environment", 1) < 0)
        return 150;
    if (api->waitpid(child, &status) != child || status != 42 ||
        process_child_phase != 2)
        return 151;
    if (cb_test_current_wake_reason() != CB_WAKE_CHILD_EXITED)
        return 152;
    if (api->waitpid(child, &status) != -1 ||
        api->get_errno() != CB_ECHILD)
        return 153;
    token = api->getenv("TOKEN");
    if (token == NULL || strcmp(token, "parent-environment") != 0 ||
        api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/tmp") != 0)
        return 154;

    strcpy(argument, "original-argument");
    if (api->setenv("TOKEN", "original-environment", 1) < 0 ||
        api->spawn("processchild", child_argv, NULL, NULL, 0,
                   &second_child) < 0 || second_child <= child)
        return 155;
    if (api->waitpid(second_child, &status) != second_child || status != 42)
        return 156;
    if (api->waitpid(99999, &status) != -1 ||
        api->get_errno() != CB_ECHILD)
        return 157;
    return 0;
}

static int errnochild_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    errno_child_address = api->errno_location();
    if (errno_child_address == NULL || api->get_errno() != 0 ||
        *errno_child_address != 0)
        return 179;
    *errno_child_address = CB_EACCES;
    errno_child_phase = 1;
    api->yield();
    if (api->get_errno() != CB_EACCES)
        return 180;
    errno_child_phase = 2;
    return 0;
}

static int abiprobe_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    static const int errors[] = {
        0, CB_EPERM, CB_ENOENT, CB_ENOEXEC, CB_EINTR, CB_EIO, CB_EBADF, CB_ECHILD,
        CB_ENOMEM, CB_EACCES, CB_EEXIST, CB_ENOTDIR, CB_EISDIR, CB_EINVAL,
        CB_ENFILE, CB_EMFILE, CB_ENOSPC, CB_ESPIPE, CB_EPIPE,
        CB_ENAMETOOLONG, CB_ENOSYS, CB_ENOTEMPTY
    };
    const struct cb_capabilities_v1 *capabilities;
    char *child_argv[] = {(char *)"errnochild", NULL};
    int *parent_errno;
    cb_pid_t child;
    int status;
    size_t index;
    (void)argc;
    (void)argv;

    if (api == NULL || api->abi_version != CB_ABI_VERSION_V1 ||
        api->struct_size != sizeof(*api) || api->getpid == NULL ||
        api->getppid == NULL || api->spawn == NULL || api->exec == NULL ||
        api->exit == NULL || api->waitpid == NULL || api->yield == NULL ||
        api->open == NULL || api->close == NULL || api->read == NULL ||
        api->write == NULL || api->lseek == NULL || api->dup == NULL ||
        api->dup2 == NULL || api->set_cloexec == NULL || api->pipe == NULL ||
        api->fstat == NULL || api->stat == NULL || api->mkdir == NULL ||
        api->unlink == NULL || api->chdir == NULL || api->getcwd == NULL ||
        api->getenv == NULL || api->setenv == NULL || api->unsetenv == NULL ||
        api->strerror == NULL || api->get_errno == NULL ||
        api->set_errno == NULL || api->capabilities == NULL ||
        api->allocate == NULL || api->resize == NULL ||
        api->release == NULL || api->errno_location == NULL ||
        api->environ_location == NULL || api->getopt_state_location == NULL ||
        api->truncate == NULL || api->ftruncate == NULL ||
        api->getprogname == NULL || api->opendir == NULL ||
        api->readdir == NULL || api->closedir == NULL)
        return 181;
    capabilities = api->capabilities();
    if (capabilities == NULL ||
        capabilities->abi_version != CB_ABI_VERSION_V1 ||
        capabilities->struct_size != sizeof(*capabilities) ||
        capabilities->native_modules != 1 ||
        capabilities->cooperative_tasks != 1 ||
        capabilities->memory_protection != 0 || capabilities->spawn != 1 ||
        capabilities->exec != 1 || capabilities->fork != 0 ||
        capabilities->vfork != 0 || capabilities->ramfs != 1 ||
        capabilities->persistent_fs != 0 || capabilities->host_mounts != 0 ||
        capabilities->network_sockets != 0 ||
        capabilities->unix_sockets != 0 || capabilities->pty != 0 ||
        capabilities->wasm_executor != 0 || capabilities->x11 != 0)
        return 182;
    for (index = 0; index < sizeof(errors) / sizeof(errors[0]); ++index)
        if (strcmp(api->strerror(errors[index]), "unknown error") == 0)
            return 183;
    if (strcmp(api->strerror(123456), "unknown error") != 0)
        return 184;

    parent_errno = api->errno_location();
    if (parent_errno == NULL || *parent_errno != 0)
        return 226;
    errno_child_phase = 0;
    errno_child_address = NULL;
    if (api->spawn("errnochild", child_argv, envp, NULL, 0, &child) < 0)
        return 185;
    *parent_errno = CB_EPERM;
    api->yield();
    if (errno_child_phase != 1 || errno_child_address == NULL ||
        errno_child_address == parent_errno || *parent_errno != CB_EPERM ||
        api->get_errno() != CB_EPERM)
        return 186;
    api->yield();
    if (errno_child_phase != 2 || api->get_errno() != CB_EPERM)
        return 187;
    if (api->waitpid(child, &status) != child || status != 0 ||
        api->get_errno() != 0)
        return 188;
    return 0;
}

static int overflowprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    unsigned char byte = 0;
    int descriptor;
    (void)argc;
    (void)argv;
    (void)envp;
    descriptor = api->open("/tmp/overflow",
                           CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC, 0600);
    if (descriptor < 0)
        return 191;
    if (api->read(descriptor, &byte, SIZE_MAX) != -1 ||
        api->get_errno() != CB_EINVAL)
        return 192;
    if (api->write(descriptor, &byte, SIZE_MAX) != -1 ||
        api->get_errno() != CB_EINVAL)
        return 193;
    if (api->lseek(descriptor, INT64_MAX, CB_SEEK_SET) != INT64_MAX ||
        api->lseek(descriptor, 1, CB_SEEK_CUR) != -1 ||
        api->get_errno() != CB_EINVAL)
        return 194;
    if (api->close(descriptor) < 0)
        return 195;
    return 0;
}

static int allocationchild_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    unsigned char *memory;
    (void)argc;
    (void)argv;
    (void)envp;
    api->release(allocation_foreign_pointer);
    if (api->get_errno() != CB_EINVAL)
        return 221;
    memory = api->allocate(16);
    if (memory == NULL)
        return 202;
    memory[0] = 0x5a;
    allocation_child_phase = 1;
    api->yield();
    if (memory[0] != 0x5a)
        return 203;
    allocation_child_phase = 2;
    return 0;
}

static int allocationafterexec_main(const struct cb_api_v1 *api, int argc,
                                    char *const argv[], char *const envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    return cb_test_task_allocation_count(api->getpid()) == 0 &&
           api->errno_location() != NULL && *api->errno_location() == 0 ?
           0 : 204;
}

static int allocationexec_main(const struct cb_api_v1 *api, int argc,
                               char *const argv[], char *const envp[])
{
    char *replacement_argv[] = {(char *)"allocationafterexec", NULL};
    (void)argc;
    (void)argv;
    if (api->allocate(23) == NULL ||
        cb_test_task_allocation_count(api->getpid()) != 1)
        return 205;
    api->set_errno(CB_EACCES);
    if (api->exec("allocationafterexec", replacement_argv, envp) < 0)
        return 206;
    return 207;
}

static int allocationprobe_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    unsigned char *memory;
    unsigned char stack_byte = 0;
    char *child_argv[] = {(char *)"allocationchild", NULL};
    char *exec_argv[] = {(char *)"allocationexec", NULL};
    cb_pid_t child;
    int status;
    size_t index;
    (void)argc;
    (void)argv;

    memory = api->allocate(8);
    if (memory == NULL || cb_test_task_allocation_count(api->getpid()) != 1)
        return 208;
    for (index = 0; index < 8; ++index)
        memory[index] = (unsigned char)(index + 1);
    memory = api->resize(memory, 32);
    if (memory == NULL || cb_test_task_allocation_count(api->getpid()) != 1)
        return 209;
    for (index = 0; index < 8; ++index)
        if (memory[index] != (unsigned char)(index + 1))
            return 210;
    api->release(memory);
    if (api->get_errno() != 0 ||
        cb_test_task_allocation_count(api->getpid()) != 0)
        return 211;
    memory = api->resize(NULL, 0);
    if (memory == NULL || cb_test_task_allocation_count(api->getpid()) != 1)
        return 212;
    if (api->resize(memory, 0) != NULL || api->get_errno() != 0 ||
        cb_test_task_allocation_count(api->getpid()) != 0)
        return 213;
    api->release(NULL);
    if (api->get_errno() != 0)
        return 214;
    api->release(&stack_byte);
    if (api->get_errno() != CB_EINVAL)
        return 215;

    allocation_failure_countdown = 0;
    if (api->allocate(4) != NULL || api->get_errno() != CB_ENOMEM ||
        cb_test_task_allocation_count(api->getpid()) != 0)
        return 222;
    allocation_failure_countdown = 1;
    if (api->allocate(4) != NULL || api->get_errno() != CB_ENOMEM ||
        cb_test_task_allocation_count(api->getpid()) != 0)
        return 223;

    allocation_child_phase = 0;
    allocation_foreign_pointer = api->allocate(7);
    if (allocation_foreign_pointer == NULL)
        return 224;
    if (api->spawn("allocationchild", child_argv, envp, NULL, 0, &child) < 0)
        return 216;
    api->yield();
    if (allocation_child_phase != 1 ||
        cb_test_task_allocation_count(child) != 1)
        return 217;
    api->yield();
    if (allocation_child_phase != 2 ||
        cb_test_task_allocation_count(child) != 0)
        return 218;
    if (api->waitpid(child, &status) != child || status != 0)
        return 219;
    if (cb_test_task_allocation_count(api->getpid()) != 1)
        return 225;
    api->release(allocation_foreign_pointer);
    allocation_foreign_pointer = NULL;

    if (api->spawn("allocationexec", exec_argv, envp, NULL, 0, &child) < 0 ||
        api->waitpid(child, &status) != child || status != 0)
        return 220;
    return 0;
}

static int libcallocation_main(int argc, char *argv[])
{
    unsigned char *zeroed;
    unsigned char *memory;
    unsigned char *resized;
    unsigned char foreign = 0;
    size_t index;
    (void)argc;
    (void)argv;

    zeroed = cb_libc_calloc(3, 5);
    if (zeroed == NULL)
        return 234;
    for (index = 0; index < 15; ++index)
        if (zeroed[index] != 0)
            return 235;
    cb_libc_free(zeroed);

    if (cb_libc_calloc(SIZE_MAX, 2) != NULL ||
        *cb_libc_errno_location() != CB_ENOMEM)
        return 236;
    zeroed = cb_libc_calloc(0, SIZE_MAX);
    if (zeroed == NULL)
        return 237;
    cb_libc_free(zeroed);

    memory = cb_libc_malloc(4);
    if (memory == NULL)
        return 238;
    for (index = 0; index < 4; ++index)
        memory[index] = (unsigned char)(0x40 + index);
    resize_failure_countdown = 0;
    if (cb_libc_realloc(memory, 8) != NULL ||
        *cb_libc_errno_location() != CB_ENOMEM)
        return 239;
    for (index = 0; index < 4; ++index)
        if (memory[index] != (unsigned char)(0x40 + index))
            return 240;
    resized = cb_libc_realloc(memory, 8);
    if (resized == NULL)
        return 241;
    for (index = 0; index < 4; ++index)
        if (resized[index] != (unsigned char)(0x40 + index))
            return 242;
    resized = cb_libc_realloc(resized, 2);
    if (resized == NULL || resized[0] != 0x40 || resized[1] != 0x41)
        return 243;
    if (cb_libc_realloc(resized, 0) != NULL ||
        *cb_libc_errno_location() != 0)
        return 244;
    if (cb_libc_realloc(&foreign, 2) != NULL ||
        *cb_libc_errno_location() != CB_EINVAL)
        return 245;
    memory = cb_libc_realloc(NULL, 3);
    if (memory == NULL)
        return 246;
    cb_libc_free(memory);
    return 0;
}

static int yesreader_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    char line[3];
    cb_ssize_t count;
    (void)argc;
    (void)argv;
    (void)envp;
    count = api->read(0, line, sizeof(line));
    if (count != (cb_ssize_t)sizeof(line) ||
        memcmp(line, "ok\n", sizeof(line)) != 0)
        return 227;
    return api->write(1, line, sizeof(line)) == (cb_ssize_t)sizeof(line) ?
           0 : 228;
}

static int yesprobe_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    int descriptors[2];
    char *yes_argv[] = {(char *)"yes", (char *)"ok", NULL};
    char *reader_argv[] = {(char *)"yesreader", NULL};
    struct cb_spawn_action_v1 yes_actions[3] = {{0}};
    struct cb_spawn_action_v1 reader_actions[3] = {{0}};
    cb_pid_t yes_pid;
    cb_pid_t reader_pid;
    int yes_status;
    int reader_status;
    size_t index;
    (void)argc;
    (void)argv;
    if (api->pipe(descriptors) < 0)
        return 229;
    for (index = 0; index < 3; ++index) {
        yes_actions[index].abi_version = CB_ABI_VERSION_V1;
        yes_actions[index].struct_size = sizeof(yes_actions[index]);
        reader_actions[index].abi_version = CB_ABI_VERSION_V1;
        reader_actions[index].struct_size = sizeof(reader_actions[index]);
    }
    yes_actions[0].type = CB_SPAWN_DUP2;
    yes_actions[0].from_fd = descriptors[1];
    yes_actions[0].to_fd = 1;
    yes_actions[1].type = CB_SPAWN_CLOSE;
    yes_actions[1].from_fd = descriptors[0];
    yes_actions[2].type = CB_SPAWN_CLOSE;
    yes_actions[2].from_fd = descriptors[1];

    reader_actions[0].type = CB_SPAWN_DUP2;
    reader_actions[0].from_fd = descriptors[0];
    reader_actions[0].to_fd = 0;
    reader_actions[1].type = CB_SPAWN_CLOSE;
    reader_actions[1].from_fd = descriptors[0];
    reader_actions[2].type = CB_SPAWN_CLOSE;
    reader_actions[2].from_fd = descriptors[1];

    if (api->spawn("yes", yes_argv, envp, yes_actions, 3, &yes_pid) < 0 ||
        api->spawn("yesreader", reader_argv, envp, reader_actions, 3,
                   &reader_pid) < 0)
        return 230;
    if (api->close(descriptors[0]) < 0 || api->close(descriptors[1]) < 0)
        return 231;
    if (api->waitpid(yes_pid, &yes_status) != yes_pid || yes_status != 1)
        return 232;
    if (api->waitpid(reader_pid, &reader_status) != reader_pid ||
        reader_status != 0)
        return 233;
    return 0;
}

static int stdioepipeprobe_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    int descriptors[2];
    char *child_argv[] = {(char *)"stdioprobe", (char *)"pipe", NULL};
    struct cb_spawn_action_v1 actions[3] = {{0}};
    cb_pid_t child;
    int status;
    size_t index;
    (void)argc;
    (void)argv;

    if (api->pipe(descriptors) < 0)
        return 234;
    if (api->close(descriptors[0]) < 0)
        return 235;
    for (index = 0; index < 3; ++index) {
        actions[index].abi_version = CB_ABI_VERSION_V1;
        actions[index].struct_size = sizeof(actions[index]);
    }
    actions[0].type = CB_SPAWN_DUP2;
    actions[0].from_fd = descriptors[1];
    actions[0].to_fd = 1;
    actions[1].type = CB_SPAWN_CLOSE;
    actions[1].from_fd = descriptors[0];
    actions[2].type = CB_SPAWN_CLOSE;
    actions[2].from_fd = descriptors[1];
    if (api->spawn("stdioprobe", child_argv, envp, actions, 3, &child) < 0)
        return 236;
    if (api->close(descriptors[1]) < 0)
        return 237;
    if (api->waitpid(child, &status) != child || status != 0)
        return 238;
    return 0;
}

static int ramfsprobe_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    static const unsigned char initial_data[] = "abcdef";
    static const unsigned char sparse_data[] = {
        'a', 'b', 'c', 'd', 'e', 'f', 0, 0, 0, 0, 'Z'
    };
    static const unsigned char truncated_sparse_data[] = {0, 0, 0, 0, 'Z'};
    static const unsigned char appended_sparse_data[] = {0, 0, 0, 0, 'Z', '!'};
    const char *initial_directories[] = {
        "/", "/bin", "/tmp", "/home", "/home/user"
    };
    const uint32_t initial_modes[] = {0755, 0755, 0777, 0755, 0755};
    struct cb_stat_v1 initial_stats[5];
    struct cb_stat_v1 path_stat;
    struct cb_stat_v1 descriptor_stat;
    unsigned char buffer[32];
    char cwd[CB_PATH_MAX];
    int descriptor;
    int second_descriptor;
    size_t index;
    (void)argc;
    (void)argv;
    (void)envp;

    for (index = 0; index < 5; ++index) {
        if (api->stat(initial_directories[index], &initial_stats[index]) < 0 ||
            initial_stats[index].abi_version != CB_ABI_VERSION_V1 ||
            initial_stats[index].struct_size != sizeof(struct cb_stat_v1) ||
            initial_stats[index].type != CB_NODE_DIRECTORY ||
            initial_stats[index].mode != initial_modes[index] ||
            initial_stats[index].size != 0 || initial_stats[index].inode == 0)
            return 160;
        if (index > 0) {
            size_t prior;
            for (prior = 0; prior < index; ++prior)
                if (initial_stats[index].inode == initial_stats[prior].inode)
                    return 161;
        }
    }
    if (api->stat("/", &path_stat) < 0 ||
        path_stat.inode != initial_stats[0].inode ||
        api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
        return 162;

    if (api->mkdir("/tmp/work", 0711) < 0 ||
        api->chdir("/tmp/work") < 0 ||
        api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/tmp/work") != 0 ||
        api->stat(".", &path_stat) < 0 || path_stat.type != CB_NODE_DIRECTORY ||
        path_stat.mode != 0711 || api->mkdir("../peer", 0700) < 0 ||
        api->stat("/tmp/peer", &descriptor_stat) < 0 ||
        descriptor_stat.type != CB_NODE_DIRECTORY || descriptor_stat.mode != 0700)
        return 163;
    if (api->chdir("../../../../") < 0 ||
        api->getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
        return 164;
    if (api->mkdir("/tmp/work", 0755) != -1 ||
        api->get_errno() != CB_EEXIST)
        return 165;

    descriptor = api->open("/tmp/work/first",
                           CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC, 0640);
    if (descriptor < 0 ||
        api->write(descriptor, initial_data, sizeof(initial_data) - 1) !=
            (cb_ssize_t)(sizeof(initial_data) - 1) ||
        api->fstat(descriptor, &descriptor_stat) < 0 ||
        api->stat("/tmp/work/first", &path_stat) < 0 ||
        descriptor_stat.inode != path_stat.inode ||
        descriptor_stat.type != CB_NODE_REGULAR || descriptor_stat.mode != 0640 ||
        descriptor_stat.size != sizeof(initial_data) - 1)
        return 166;
    if (api->lseek(descriptor, 10, CB_SEEK_SET) != 10 ||
        api->write(descriptor, "Z", 1) != 1 ||
        api->lseek(descriptor, 0, CB_SEEK_SET) != 0 ||
        api->read(descriptor, buffer, sizeof(sparse_data)) !=
            (cb_ssize_t)sizeof(sparse_data) ||
        memcmp(buffer, sparse_data, sizeof(sparse_data)) != 0)
        return 167;
    if (api->close(descriptor) < 0)
        return 168;

    descriptor = api->open("/tmp/work/first", CB_O_WRONLY | CB_O_TRUNC, 0);
    if (descriptor < 0 || api->lseek(descriptor, 20, CB_SEEK_SET) != 20 ||
        api->write(descriptor, NULL, 0) != 0 ||
        api->fstat(descriptor, &descriptor_stat) < 0 ||
        descriptor_stat.size != 0 ||
        api->lseek(descriptor, 4, CB_SEEK_SET) != 4 ||
        api->write(descriptor, "Z", 1) != 1 || api->close(descriptor) < 0)
        return 169;
    descriptor = api->open("/tmp/work/first", CB_O_RDONLY, 0);
    if (descriptor < 0 ||
        api->read(descriptor, buffer, sizeof(truncated_sparse_data)) !=
            (cb_ssize_t)sizeof(truncated_sparse_data) ||
        memcmp(buffer, truncated_sparse_data, sizeof(truncated_sparse_data)) != 0)
        return 170;

    second_descriptor = api->open("/tmp/work/second",
                                  CB_O_RDWR | CB_O_CREAT | CB_O_TRUNC, 0600);
    if (second_descriptor < 0 || api->write(second_descriptor, "Q", 1) != 1 ||
        api->lseek(second_descriptor, 0, CB_SEEK_SET) != 0 ||
        api->read(second_descriptor, buffer, 1) != 1 || buffer[0] != 'Q' ||
        api->fstat(second_descriptor, &descriptor_stat) < 0 ||
        descriptor_stat.mode != 0600 || descriptor_stat.size != 1 ||
        descriptor_stat.inode == path_stat.inode)
        return 171;
    if (api->close(descriptor) < 0 || api->close(second_descriptor) < 0)
        return 172;

    descriptor = api->open("/tmp/work/first", CB_O_WRONLY | CB_O_APPEND, 0);
    if (descriptor < 0 || api->lseek(descriptor, 0, CB_SEEK_SET) != 0 ||
        api->write(descriptor, "!", 1) != 1 ||
        api->read(descriptor, buffer, 1) != -1 || api->get_errno() != CB_EBADF ||
        api->close(descriptor) < 0)
        return 173;
    descriptor = api->open("/tmp/work/first", CB_O_RDONLY, 0);
    if (descriptor < 0 || api->write(descriptor, "x", 1) != -1 ||
        api->get_errno() != CB_EBADF ||
        api->fstat(descriptor, &descriptor_stat) < 0 ||
        descriptor_stat.size != sizeof(appended_sparse_data) ||
        descriptor_stat.inode != path_stat.inode ||
        api->read(descriptor, buffer, sizeof(appended_sparse_data)) !=
            (cb_ssize_t)sizeof(appended_sparse_data) ||
        memcmp(buffer, appended_sparse_data, sizeof(appended_sparse_data)) != 0 ||
        api->close(descriptor) < 0)
        return 174;

    if (api->mkdir("/tmp/work/first/child", 0755) != -1 ||
        api->get_errno() != CB_ENOTDIR ||
        api->open("/tmp/work", CB_O_RDONLY, 0) != -1 ||
        api->get_errno() != CB_EISDIR)
        return 175;
    if (api->unlink("/tmp/missing") != -1 || api->get_errno() != CB_ENOENT ||
        api->unlink("/tmp/peer") != -1 || api->get_errno() != CB_EISDIR ||
        api->unlink("/tmp/work") != -1 || api->get_errno() != CB_ENOTEMPTY ||
        api->unlink("/") != -1 || api->get_errno() != CB_ENOTEMPTY)
        return 176;
    if (api->stat(NULL, &path_stat) != -1 || api->get_errno() != CB_EINVAL ||
        api->stat("/tmp/work/first", NULL) != -1 ||
        api->get_errno() != CB_EINVAL)
        return 177;
    if (api->chdir("/home/user") < 0 || api->getcwd(cwd, 2) != NULL ||
        api->get_errno() != CB_ENAMETOOLONG)
        return 178;
    return 0;
}

static const struct cb_program_v1 pidcheck_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pidcheck", 0,
    64 * 1024, pidcheck_main
};

static const struct cb_program_v1 execprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "execprobe", 0,
    64 * 1024, execprobe_main
};

static const struct cb_program_v1 unlinkprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "unlinkprobe", 0,
    64 * 1024, unlinkprobe_main
};

static const struct cb_program_v1 pipeallocprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipeallocprobe", 0,
    64 * 1024, pipeallocprobe_main
};

static const struct cb_program_v1 pipezeropeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipezeropeer", 0,
    64 * 1024, pipezeropeer_main
};

static const struct cb_program_v1 pipezeroprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipezeroprobe", 0,
    64 * 1024, pipezeroprobe_main
};


static int poll_wake_write_fd;

static int pollwakepeer_main(const struct cb_api_v1 *api, int argc,
                             char *const argv[], char *const envp[])
{
    char byte = 'x';
    (void)argc;
    (void)argv;
    (void)envp;
    for (int i = 0; i < 50; i++) {
        api->yield();
    }
    if (api->write(poll_wake_write_fd, &byte, 1) != 1) return 1;
    return 0;
}

static int pollwakeprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    char *peer_argv[] = {(char *)"pollwakepeer", NULL};
    struct cb_spawn_action_v1 close_reader;
    cb_pid_t peer;
    int descriptors[2];
    int status;
    struct cb_pollfd pfd;
    (void)argc;
    (void)argv;

    if (api->pipe(descriptors) < 0) return 91;
    poll_wake_write_fd = descriptors[1];

    close_reader.abi_version = CB_ABI_VERSION_V1;
    close_reader.struct_size = sizeof(close_reader);
    close_reader.type = CB_SPAWN_CLOSE;
    close_reader.from_fd = descriptors[0];

    if (api->spawn(peer_argv[0], peer_argv, envp, &close_reader, 1, &peer) < 0) return 92;
    if (api->close(descriptors[1]) < 0) return 93;

    pfd.fd = descriptors[0];
    pfd.events = CB_POLLIN;
    /* Block until the peer writes */
    if (api->poll(&pfd, 1, 5000) != 1) return 94;
    if (!(pfd.revents & CB_POLLIN)) return 95;

    if (api->waitpid(peer, &status) != peer || status != 0) return 96;
    if (api->close(descriptors[0]) < 0) return 97;
    return 0;
}

static const struct cb_program_v1 pollwakepeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pollwakepeer", 0,
    64 * 1024, pollwakepeer_main
};

static const struct cb_program_v1 pollwakeprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pollwakeprobe", 0,
    64 * 1024, pollwakeprobe_main
};

static const struct cb_program_v1 pipeedgepeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipeedgepeer", 0,
    64 * 1024, pipeedgepeer_main
};

static const struct cb_program_v1 pipeedgeprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipeedgeprobe", 0,
    64 * 1024, pipeedgeprobe_main
};

static const struct cb_program_v1 pipecapacitypeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipecapacitypeer", 0,
    64 * 1024, pipecapacitypeer_main
};

static const struct cb_program_v1 pipecapacityprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "pipecapacityprobe", 0,
    64 * 1024, pipecapacityprobe_main
};

static const struct cb_program_v1 environpeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "environpeer", 0,
    64 * 1024, environpeer_main
};

static const struct cb_program_v1 environprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "environprobe", 0,
    64 * 1024, environprobe_main
};

static const struct cb_program_v1 exitwaitprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "exitwaitprobe", 0,
    64 * 1024, exitwaitprobe_main
};

static const struct cb_program_v1 getoptwaitprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "getoptwaitprobe", 0,
    64 * 1024, getoptwaitprobe_main
};

static const struct cb_program_v1 getopterrprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "getopterrprobe", 0,
    64 * 1024, getopterrprobe_main
};

static const struct cb_program_v1 getoptclusterprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "getoptclusterprobe", 0,
    64 * 1024, getoptclusterprobe_main
};

static const struct cb_program_v1 errxprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "errxprobe", 0,
    64 * 1024, errxprobe_main
};

static const struct cb_program_v1 direntbasicprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntbasicprobe", 0,
    64 * 1024, direntbasicprobe_main
};

static const struct cb_program_v1 direntmutationprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntmutationprobe", 0,
    64 * 1024, direntmutationprobe_main
};

static const struct cb_program_v1 direntisolationchild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntisolationchild", 0,
    64 * 1024, direntisolationchild_main
};

static const struct cb_program_v1 direntisolationprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntisolationprobe", 0,
    64 * 1024, direntisolationprobe_main
};

static const struct cb_program_v1 direntoldtableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntoldtableprobe", 0,
    64 * 1024, direntoldtableprobe_main
};

static const struct cb_program_v1 direntopendirnulltableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1),
    "direntopendirnulltableprobe", 0, 64 * 1024,
    direntopendirnulltableprobe_main
};

static const struct cb_program_v1 direntreaddirnulltableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1),
    "direntreaddirnulltableprobe", 0, 64 * 1024,
    direntreaddirnulltableprobe_main
};

static const struct cb_program_v1 direntclosedirnulltableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1),
    "direntclosedirnulltableprobe", 0, 64 * 1024,
    direntclosedirnulltableprobe_main
};

static const struct cb_program_v1 direntclosedirrebindprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1),
    "direntclosedirrebindprobe", 0, 64 * 1024,
    direntclosedirrebindprobe_main
};

static const struct cb_program_v1 direntlibcallocfailprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1),
    "direntlibcallocfailprobe", 0, 64 * 1024, direntlibcallocfailprobe_main
};

static const struct cb_program_v1 direntreapexit_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapexit", 0,
    64 * 1024, direntreapexit_main
};

static const struct cb_program_v1 direntreapexitboot_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapexitboot", 0,
    64 * 1024, direntreapexitboot_main
};

static const struct cb_program_v1 direntreapexecpeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapexecpeer", 0,
    64 * 1024, direntreapexecpeer_main
};

static const struct cb_program_v1 direntreapexec_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapexec", 0,
    64 * 1024, direntreapexec_main
};

static const struct cb_program_v1 direntreapdestroychild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapdestroychild",
    0, 64 * 1024, direntreapdestroychild_main
};

static const struct cb_program_v1 direntreapdestroyboot_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "direntreapdestroyboot",
    0, 64 * 1024, direntreapdestroyboot_main
};

static const struct cb_program_v1 terminalprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "terminalprobe", 0,
    64 * 1024, terminalprobe_main
};

static const struct cb_program_v1 terminalpeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "terminalpeer", 0,
    64 * 1024, terminalpeer_main
};

static const struct cb_program_v1 descriptorchild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "descriptorchild", 0,
    64 * 1024, descriptorchild_main
};

static const struct cb_program_v1 descriptorprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "descriptorprobe", 0,
    64 * 1024, descriptorprobe_main
};

static const struct cb_program_v1 processchild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "processchild", 0,
    64 * 1024, processchild_main
};

static const struct cb_program_v1 processprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "processprobe", 0,
    64 * 1024, processprobe_main
};

static const struct cb_program_v1 ramfsprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "ramfsprobe", 0,
    64 * 1024, ramfsprobe_main
};

static const struct cb_program_v1 errnochild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "errnochild", 0,
    64 * 1024, errnochild_main
};

static const struct cb_program_v1 abiprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "abiprobe", 0,
    64 * 1024, abiprobe_main
};

static const struct cb_program_v1 overflowprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "overflowprobe", 0,
    64 * 1024, overflowprobe_main
};

static const struct cb_program_v1 allocationchild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "allocationchild", 0,
    64 * 1024, allocationchild_main
};

static const struct cb_program_v1 allocationafterexec_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "allocationafterexec", 0,
    64 * 1024, allocationafterexec_main
};

static const struct cb_program_v1 allocationexec_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "allocationexec", 0,
    64 * 1024, allocationexec_main
};

static const struct cb_program_v1 allocationprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "allocationprobe", 0,
    64 * 1024, allocationprobe_main
};

int cb_stdio_test_main(int argc, char *argv[]);
CB_LIBC_PROGRAM(stdioprobe_program, "stdioprobe", cb_stdio_test_main);

static const struct cb_program_v1 stdioepipeprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdioepipeprobe", 0,
    64 * 1024, stdioepipeprobe_main
};

CB_LIBC_PROGRAM(libcallocprobe_program, "libcallocprobe",
                libcallocation_main);

static const struct cb_program_v1 yesreader_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "yesreader", 0,
    64 * 1024, yesreader_main
};

static const struct cb_program_v1 yesprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "yesprobe", 0,
    64 * 1024, yesprobe_main
};

#define TRUNCATE_CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "truncate: %s\n", message); \
        return 1; \
    } \
} while (0)

static int truncateprobe_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    const char *path = "/tmp/resize";
    unsigned char original[100], data[320], snapshot[11];
    struct cb_stat_v1 status;
    int fd, reader, duplicate, append, pipes[2], result, saved_error;
    size_t index;
    (void)argc; (void)argv; (void)envp;
    for (index = 0; index < sizeof(original); ++index)
        original[index] = (unsigned char)(index + 1);
    fd = api->open(path, CB_O_CREAT | CB_O_RDWR, 0400);
    TRUNCATE_CHECK(fd >= 0 && api->write(fd, original, sizeof(original)) == 100,
                   "create baseline data; mode-bit enforcement remains deferred");
    reader = api->open(path, CB_O_RDONLY, 0);
    duplicate = api->dup(fd);
    TRUNCATE_CHECK(reader >= 0 && duplicate >= 0 &&
                   api->lseek(reader, 80, CB_SEEK_SET) == 80 &&
                   api->lseek(duplicate, 80, CB_SEEK_SET) == 80,
                   "independent and shared offsets");
    TRUNCATE_CHECK(api->ftruncate(duplicate, 10) == 0 &&
                   api->lseek(fd, 0, CB_SEEK_CUR) == 80 &&
                   api->lseek(reader, 0, CB_SEEK_CUR) == 80 &&
                   api->fstat(reader, &status) == 0 && status.size == 10 &&
                   api->read(fd, data, 1) == 0,
                   "shrink visible through independent fd; offsets unchanged and EOF");
    TRUNCATE_CHECK(api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 10 &&
                   memcmp(data, original, 10) == 0,
                   "shrink preserves prefix");
    TRUNCATE_CHECK(api->ftruncate(fd, 50) == 0 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 50 &&
                   memcmp(data, original, 10) == 0,
                   "regrow within old capacity");
    for (index = 10; index < 50; ++index)
        TRUNCATE_CHECK(data[index] == 0, "regrow exposes zeros, not stale bytes");
    TRUNCATE_CHECK(api->ftruncate(fd, 300) == 0 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 300,
                   "growth requiring a new allocation");
    for (index = 10; index < 300; ++index)
        TRUNCATE_CHECK(data[index] == 0, "allocated growth is zero-filled");

    TRUNCATE_CHECK(api->lseek(reader, 77, CB_SEEK_SET) == 77 &&
                   api->truncate(path, 5) == 0 &&
                   api->fstat(reader, &status) == 0 && status.size == 5 &&
                   api->lseek(reader, 0, CB_SEEK_CUR) == 77 &&
                   api->lseek(fd, 0, CB_SEEK_CUR) == 80,
                   "path truncate leaves independently open and shared offsets unchanged");
    TRUNCATE_CHECK(api->write(fd, "Z", 1) == 1 &&
                   api->lseek(duplicate, 0, CB_SEEK_CUR) == 81 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 81 &&
                   memcmp(data, original, 5) == 0 && data[80] == 'Z',
                   "write past shrunk EOF keeps offset and prefix");
    for (index = 5; index < 80; ++index)
        TRUNCATE_CHECK(data[index] == 0, "write after shrink zero-fills hole");

    append = api->open(path, CB_O_WRONLY | CB_O_APPEND, 0);
    TRUNCATE_CHECK(append >= 0 && api->ftruncate(fd, 2) == 0 &&
                   api->write(append, "!", 1) == 1 &&
                   api->truncate(path, 10) == 0 &&
                   api->write(append, "A", 1) == 1 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, snapshot, sizeof(snapshot)) == 11 &&
                   snapshot[0] == 1 && snapshot[1] == 2 &&
                   snapshot[2] == '!' && snapshot[10] == 'A',
                   "append follows new end after shrink and growth");
    for (index = 3; index < 10; ++index)
        TRUNCATE_CHECK(snapshot[index] == 0, "append growth gap zeros");
    TRUNCATE_CHECK(api->ftruncate(append, 11) == 0, "write-only descriptor can truncate");
    TRUNCATE_CHECK(api->ftruncate(reader, 0) == -1 && api->get_errno() == CB_EBADF,
                   "read-only descriptor");
    TRUNCATE_CHECK(api->truncate("/tmp", 0) == -1 && api->get_errno() == CB_EISDIR,
                   "directory path");
    TRUNCATE_CHECK(api->truncate("/missing", 0) == -1 && api->get_errno() == CB_ENOENT,
                   "missing path");
    TRUNCATE_CHECK(api->truncate("/tmp/resize/child", 0) == -1 && api->get_errno() == CB_ENOTDIR,
                   "non-directory path component");
    TRUNCATE_CHECK(api->truncate(NULL, 0) == -1 && api->get_errno() == CB_EINVAL,
                   "NULL path");
    TRUNCATE_CHECK(api->truncate(path, -1) == -1 && api->get_errno() == CB_EINVAL &&
                   api->ftruncate(fd, -1) == -1 && api->get_errno() == CB_EINVAL,
                   "negative lengths rejected before narrowing");
    TRUNCATE_CHECK(api->ftruncate(-1, 0) == -1 && api->get_errno() == CB_EBADF &&
                   api->ftruncate(CB_MAX_FDS, 0) == -1 && api->get_errno() == CB_EBADF,
                   "invalid descriptors");
    TRUNCATE_CHECK(api->ftruncate(0, 0) == -1 && api->get_errno() == CB_ESPIPE &&
                   api->ftruncate(1, 0) == -1 && api->get_errno() == CB_ESPIPE,
                   "terminal descriptors");
    TRUNCATE_CHECK(api->pipe(pipes) == 0 &&
                   api->ftruncate(pipes[0], 0) == -1 && api->get_errno() == CB_ESPIPE &&
                   api->ftruncate(pipes[1], 0) == -1 && api->get_errno() == CB_ESPIPE &&
                   api->close(pipes[0]) == 0 && api->close(pipes[1]) == 0,
                   "both pipe directions");
    {
        struct cb_open_file *file = truncate_test_kernel->current->descriptors[fd].file;
        const struct cb_file_ops *ops = file->ops;
        struct cb_file_ops without_truncate = *ops;
        without_truncate.truncate = NULL;
        file->ops = &without_truncate;
        result = api->ftruncate(fd, 1);
        saved_error = api->get_errno();
        file->ops = ops;
        TRUNCATE_CHECK(result == -1 && saved_error == CB_EBADF,
                       "descriptor missing operation uses EBADF");
    }
    TRUNCATE_CHECK(api->lseek(fd, 7, CB_SEEK_SET) == 7, "failure offset baseline");
    resize_failure_countdown = 0;
    TRUNCATE_CHECK(api->ftruncate(fd, 4096) == -1 && api->get_errno() == CB_ENOMEM &&
                   api->lseek(fd, 0, CB_SEEK_CUR) == 7 &&
                   api->fstat(reader, &status) == 0 && status.size == 11 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 11 &&
                   memcmp(data, snapshot, 11) == 0,
                   "allocation failure preserves size, data, and offset");
    resize_failure_countdown = 0;
    resize_request_size = 0;
    if (sizeof(size_t) < sizeof(cb_off_t)) {
        TRUNCATE_CHECK(api->ftruncate(fd, INT64_MAX) == -1 &&
                       api->get_errno() == CB_EINVAL && resize_request_size == 0 &&
                       api->truncate(path, INT64_MAX) == -1 &&
                       api->get_errno() == CB_EINVAL && resize_request_size == 0,
                       "length wider than size_t never reaches allocator");
        TRUNCATE_CHECK(api->ftruncate(fd, (cb_off_t)SIZE_MAX) == -1 &&
                       api->get_errno() == CB_ENOMEM && resize_request_size == SIZE_MAX,
                       "maximum representable length does not wrap capacity growth");
    } else {
        TRUNCATE_CHECK(api->truncate(path, INT64_MAX) == -1 &&
                       api->get_errno() == CB_ENOMEM &&
                       (uint64_t)resize_request_size >= (uint64_t)INT64_MAX,
                       "capacity arithmetic does not wrap before failed allocation");
    }
    TRUNCATE_CHECK(api->fstat(reader, &status) == 0 && status.size == 11 &&
                   api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 11 &&
                   memcmp(data, snapshot, 11) == 0,
                   "oversized failure preserves bytes");
    resize_failure_countdown = 0;
    TRUNCATE_CHECK(api->ftruncate(fd, 11) == 0 && api->ftruncate(fd, 0) == 0 &&
                   api->ftruncate(fd, 11) == 0 && resize_failure_countdown == 0,
                   "same size, shrink, and spare-capacity regrowth do not allocate");
    resize_failure_countdown = -1;
    TRUNCATE_CHECK(api->lseek(reader, 0, CB_SEEK_SET) == 0 &&
                   api->read(reader, data, sizeof(data)) == 11,
                   "read regrowth after zero shrink");
    for (index = 0; index < 11; ++index)
        TRUNCATE_CHECK(data[index] == 0, "zero shrink discards all former bytes");
    TRUNCATE_CHECK(api->unlink(path) == 0 && api->ftruncate(fd, 4) == 0 &&
                   api->fstat(reader, &status) == 0 && status.size == 4 &&
                   api->truncate(path, 1) == -1 && api->get_errno() == CB_ENOENT,
                   "unlinked open node remains resizable");
    TRUNCATE_CHECK(api->close(append) == 0 && api->close(duplicate) == 0 &&
                   api->close(reader) == 0 && api->close(fd) == 0 &&
                   api->ftruncate(fd, 1) == -1 && api->get_errno() == CB_EBADF,
                   "close cleanup and closed descriptor");
    return 0;
}

static int truncatechild_main(const struct cb_api_v1 *api, int argc,
                              char *const argv[], char *const envp[])
{
    int fd;
    struct cb_stat_v1 status;
    unsigned char bytes[8];
    size_t index;
    (void)argc; (void)argv; (void)envp;
    fd = api->open("/tmp/shared-resize", CB_O_RDWR, 0);
    TRUNCATE_CHECK(fd >= 0 && api->lseek(fd, 3, CB_SEEK_SET) == 3 &&
                   api->ftruncate(fd, 4) == 0 &&
                   api->fstat(fd, &status) == 0 && status.size == 4,
                   "child independent open shrinks shared node");
    truncate_child_phase = 1;
    api->yield();
    TRUNCATE_CHECK(api->fstat(fd, &status) == 0 && status.size == 12 &&
                   api->lseek(fd, 0, CB_SEEK_CUR) == 3 &&
                   api->lseek(fd, 4, CB_SEEK_SET) == 4 &&
                   api->read(fd, bytes, sizeof(bytes)) == 8,
                   "child sees parent path growth with own offset intact");
    for (index = 0; index < sizeof(bytes); ++index)
        TRUNCATE_CHECK(bytes[index] == 0, "shared growth zeros across tasks");
    truncate_child_phase = 2;
    return api->close(fd);
}

static int truncateinterleave_main(const struct cb_api_v1 *api, int argc,
                                   char *const argv[], char *const envp[])
{
    int fd, status;
    cb_pid_t child;
    struct cb_stat_v1 metadata;
    char *child_argv[] = {(char *)"truncatechild", NULL};
    (void)argc; (void)argv;
    fd = api->open("/tmp/shared-resize", CB_O_CREAT | CB_O_RDWR, 0600);
    TRUNCATE_CHECK(fd >= 0 && api->write(fd, "abcdefghij", 10) == 10 &&
                   api->lseek(fd, 8, CB_SEEK_SET) == 8,
                   "parent creates shared file");
    truncate_child_phase = 0;
    TRUNCATE_CHECK(api->spawn("truncatechild", child_argv, envp, NULL, 0, &child) == 0,
                   "spawn independent opener");
    api->yield();
    TRUNCATE_CHECK(truncate_child_phase == 1 &&
                   api->fstat(fd, &metadata) == 0 && metadata.size == 4 &&
                   api->lseek(fd, 0, CB_SEEK_CUR) == 8,
                   "parent sees child resize across forced interleave");
    TRUNCATE_CHECK(api->truncate("/tmp/shared-resize", 12) == 0,
                   "parent path grows shared node");
    TRUNCATE_CHECK(api->waitpid(child, &status) == child && status == 0 &&
                   truncate_child_phase == 2,
                   "child independently observed parent growth");
    return api->close(fd);
}
#undef TRUNCATE_CHECK



extern int clocklossprobe_main(int argc, char **argv);

CB_LIBC_PROGRAM(clocklossprobe_program, "clocklossprobe",
                clocklossprobe_main);

extern const struct cb_program_v1 cb_locale_probe_program;
extern const struct cb_program_v1 cb_locale_env_probe_program;
extern const struct cb_program_v1 cb_terminal_probe_program;
extern int normalpollprobe_main(int argc, char **argv);
extern int oldpollprobe_main(int argc, char **argv);

static volatile int spinner_entered = 0;
static volatile int spinner_completed = 0;

static int yieldingspinner_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    int i;
    (void)argc;
    (void)argv;
    (void)envp;
    spinner_entered = 1;
    for (i = 0; i < 200; i++) {
        api->yield();
    }
    spinner_completed = 1;
    return 0;
}

static int runnabletimeoutprobe_main(const struct cb_api_v1 *api, int argc,
                                     char *const argv[], char *const envp[])
{
    char *peer_argv[] = {(char *)"yieldingspinner", NULL};
    cb_pid_t peer;
    struct cb_pollfd pfd;
    int status;
    (void)argc;
    (void)argv;

    spinner_entered = 0;
    spinner_completed = 0;

    if (api->spawn("yieldingspinner", peer_argv, envp, NULL, 0, &peer) < 0) return 31;

    while (!spinner_entered) {
        api->yield();
    }

    pfd.fd = 0;
    pfd.events = CB_POLLIN;
    if (api->poll(&pfd, 1, 50) != 0)
        return 32;

    if (spinner_completed)
        return 40;

    if (api->waitpid(peer, &status) != peer || status != 0)
        return 34;

    return 0;
}

static const struct cb_program_v1 yieldingspinner_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "yieldingspinner", 0,
    64 * 1024, yieldingspinner_main
};

static const struct cb_program_v1 runnabletimeoutprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "runnabletimeoutprobe", 0,
    64 * 1024, runnabletimeoutprobe_main
};

CB_LIBC_PROGRAM(normalpollprobe_program, "normalpollprobe",
                normalpollprobe_main);

static int oldpollprobe_start(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct cb_api_v1 old_api = *api;
    old_api.struct_size = offsetof(struct cb_api_v1, poll);
    old_api.poll = NULL;
    (void)envp;
    return cb_libc_start(&old_api, argc, argv, oldpollprobe_main);
}

static const struct cb_program_v1 oldpollprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "oldpollprobe", 0,
    64 * 1024, oldpollprobe_start
};

static const struct cb_program_v1 truncateprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "truncateprobe", 0,
    64 * 1024, truncateprobe_main
};
static const struct cb_program_v1 truncatechild_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "truncatechild", 0,
    64 * 1024, truncatechild_main
};
static const struct cb_program_v1 truncateinterleave_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "truncateinterleave", 0,
    64 * 1024, truncateinterleave_main
};

static int dirname_noop_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 0;
}

static int dirnameoldtableprobe_main(const struct cb_api_v1 *api, int argc,
                                     char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Everything through tcsetattr (the actual current tail on this base)
       is present; dirname_buffer_location is not -- exactly the "one
       release older" cb_api_v1 cb_libc_start must still accept. */
    copy = *api;
    copy.struct_size = (uint32_t)offsetof(struct cb_api_v1,
                                          dirname_buffer_location);
    result = cb_libc_start(&copy, 0, NULL, cb_dirname_oldtable_main);
    cb_libc_start(api, 0, NULL, dirname_noop_main);
    if (result != 0)
        return 490;
    return 0;
}

static int dirnamenulltableprobe_main(const struct cb_api_v1 *api, int argc,
                                      char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Full current struct_size, but dirname_buffer_location itself NULL --
       distinct from the shrunk-struct_size case above: catches a
       regression back to "trust struct_size alone" without also
       checking the field. */
    copy = *api;
    copy.dirname_buffer_location = NULL;
    result = cb_libc_start(&copy, 0, NULL, cb_dirname_oldtable_main);
    cb_libc_start(api, 0, NULL, dirname_noop_main);
    if (result != 0)
        return 491;
    return 0;
}

static int dirnameisolationpeer_main(const struct cb_api_v1 *api, int argc,
                                     char *const argv[], char *const envp[])
{
    char path[] = "/peer/entirely/different/path";
    char *result;
    (void)argc;
    (void)argv;
    (void)envp;
    if (cb_libc_start(api, 0, NULL, dirname_noop_main) != 0)
        return 1;
    result = cb_libc_dirname(path);
    if (result == NULL || strcmp(result, "/peer/entirely/different") != 0)
        return 2;
    return 0;
}

static int dirnameisolationprobe_main(const struct cb_api_v1 *api, int argc,
                                      char *const argv[], char *const envp[])
{
    char path[] = "/parent/only/here";
    char *peer_argv[] = {(char *)"dirnameisolationpeer", NULL};
    char *result;
    cb_pid_t peer;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    if (cb_libc_start(api, 0, NULL, dirname_noop_main) != 0)
        return 1;
    result = cb_libc_dirname(path);
    if (result == NULL || strcmp(result, "/parent/only") != 0)
        return 2;

    if (api->spawn("dirnameisolationpeer", peer_argv, envp, NULL, 0,
                   &peer) < 0)
        return 3;
    /* Let the peer run its own dirname() call, with a completely
       different path, to completion before checking back on result. */
    api->yield();
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 4;

    /* result still points at THIS task's own dirname_buffer -- a stable,
       task-owned field on struct cb_task -- so it must still read back
       this task's own value. If dirname() instead returned a pointer
       into a process-wide shared static (the upstream import's own
       internal buffer, exposed directly instead of copied out), the
       peer's unrelated dirname() call above would have silently
       overwritten it by now. */
    if (strcmp(result, "/parent/only") != 0)
        return 5;
    return 0;
}

static const struct cb_program_v1 dirnameoldtableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "dirnameoldtableprobe",
    0, 64 * 1024, dirnameoldtableprobe_main
};

static const struct cb_program_v1 dirnamenulltableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "dirnamenulltableprobe",
    0, 64 * 1024, dirnamenulltableprobe_main
};

static const struct cb_program_v1 dirnameisolationpeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "dirnameisolationpeer",
    0, 64 * 1024, dirnameisolationpeer_main
};

static const struct cb_program_v1 dirnameisolationprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "dirnameisolationprobe",
    0, 64 * 1024, dirnameisolationprobe_main
};

static int basenameoldtableprobe_main(const struct cb_api_v1 *api, int argc,
                                      char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Everything through closedir (the actual current tail on this base)
       is present; basename_buffer_location is not -- the "one release
       older" cb_api_v1 cb_libc_start must still accept. */
    copy = *api;
    copy.struct_size = (uint32_t)offsetof(struct cb_api_v1,
                                          basename_buffer_location);
    result = cb_libc_start(&copy, 0, NULL, cb_basename_oldtable_main);
    cb_libc_start(api, 0, NULL, dirname_noop_main);
    if (result != 0)
        return 590;
    return 0;
}

static int basenamenulltableprobe_main(const struct cb_api_v1 *api, int argc,
                                       char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)argc;
    (void)argv;
    (void)envp;

    /* Full current struct_size, but basename_buffer_location itself
       NULL -- distinct from the shrunk-struct_size case above: catches a
       regression back to "trust struct_size alone" without also
       checking the field. */
    copy = *api;
    copy.basename_buffer_location = NULL;
    result = cb_libc_start(&copy, 0, NULL, cb_basename_oldtable_main);
    cb_libc_start(api, 0, NULL, dirname_noop_main);
    if (result != 0)
        return 591;
    return 0;
}

static int basenameisolationpeer_main(const struct cb_api_v1 *api, int argc,
                                      char *const argv[], char *const envp[])
{
    char path[] = "/peer/entirely/different/name";
    char *result;
    (void)argc;
    (void)argv;
    (void)envp;
    if (cb_libc_start(api, 0, NULL, dirname_noop_main) != 0)
        return 1;
    result = cb_libc_basename(path);
    if (result == NULL || strcmp(result, "name") != 0)
        return 2;
    return 0;
}

static int basenameisolationprobe_main(const struct cb_api_v1 *api, int argc,
                                       char *const argv[], char *const envp[])
{
    char path[] = "/parent/only/here";
    char *peer_argv[] = {(char *)"basenameisolationpeer", NULL};
    char *result;
    cb_pid_t peer;
    int status;
    (void)argc;
    (void)argv;
    (void)envp;

    if (cb_libc_start(api, 0, NULL, dirname_noop_main) != 0)
        return 1;
    result = cb_libc_basename(path);
    if (result == NULL || strcmp(result, "here") != 0)
        return 2;

    if (api->spawn("basenameisolationpeer", peer_argv, envp, NULL, 0,
                   &peer) < 0)
        return 3;
    /* Let the peer run its own basename() call, with a completely
       different path, to completion before checking back on result. */
    api->yield();
    if (api->waitpid(peer, &status) != peer || status != 0)
        return 4;

    /* result still points at THIS task's own basename_buffer -- a
       stable, task-owned field on struct cb_task, separate from
       dirname's own buffer -- so it must still read back this task's
       own value. */
    if (strcmp(result, "here") != 0)
        return 5;
    return 0;
}

static const struct cb_program_v1 basenameoldtableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "basenameoldtableprobe",
    0, 64 * 1024, basenameoldtableprobe_main
};

static const struct cb_program_v1 basenamenulltableprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "basenamenulltableprobe",
    0, 64 * 1024, basenamenulltableprobe_main
};

static const struct cb_program_v1 basenameisolationpeer_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "basenameisolationpeer",
    0, 64 * 1024, basenameisolationpeer_main
};

static const struct cb_program_v1 basenameisolationprobe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "basenameisolationprobe",
    0, 64 * 1024, basenameisolationprobe_main
};

enum test_fixture {
    FIXTURE_BASE = 0,
    FIXTURE_FULL = 1,
    FIXTURE_MAC = 2,
    FIXTURE_DIRNAME = 3,
    FIXTURE_DIRENT = 4,
    FIXTURE_BASENAME = 5,
    FIXTURE_YES = 6
};

/* Keep the shared Mac suite independent of the full 64-slot native fixture.
 * Every new shared probe must be explicitly registered here and in Mac main. */
static int register_mac_probes(struct cb_kernel *kernel)
{
    return cb_kernel_register(kernel, &cb_getopt_arg_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_argv_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_stdio_state_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_stdio_oldtable_program) == 0 &&
           cb_kernel_register(kernel, &cb_progname_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_locale_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_locale_env_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_terminal_probe_program) == 0 &&
           cb_kernel_register(kernel, &normalpollprobe_program) == 0 &&
           cb_kernel_register(kernel, &cb_err_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_strcpy_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_memory_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_getoptprobe_program) == 0 &&
           cb_kernel_register(kernel, &cb_truncate_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_dirname_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_direntprobe_program) == 0 &&
           cb_kernel_register(kernel, &cb_vfs_executable_probe_program) == 0 &&
           cb_kernel_register(kernel, &cb_basename_probe_program) == 0 ?
           0 : -1;
}

/* FIXTURE_FULL is already at its 64-slot ceiling: scope the new dirname(3)
 * coverage into its own small fixture rather than growing that limit (a
 * production struct capacity, not a test-only one) or overflowing it. */
static int register_dirname_probes(struct cb_kernel *kernel)
{
    return cb_kernel_register(kernel, &cb_dirname_probe_program) == 0 &&
           cb_kernel_register(kernel, &dirnameoldtableprobe_program) == 0 &&
           cb_kernel_register(kernel, &dirnamenulltableprobe_program) == 0 &&
           cb_kernel_register(kernel, &dirnameisolationpeer_program) == 0 &&
           cb_kernel_register(kernel, &dirnameisolationprobe_program) == 0 ?
           0 : -1;
}

/* Same reasoning as register_dirname_probes: keeps basename(3)'s coverage
 * out of FIXTURE_FULL rather than growing CB_MAX_PROGRAMS. */
static int register_basename_probes(struct cb_kernel *kernel)
{
    return cb_kernel_register(kernel, &cb_basename_probe_program) == 0 &&
           cb_kernel_register(kernel, &basenameoldtableprobe_program) == 0 &&
           cb_kernel_register(kernel, &basenamenulltableprobe_program) == 0 &&
           cb_kernel_register(kernel, &basenameisolationpeer_program) == 0 &&
           cb_kernel_register(kernel, &basenameisolationprobe_program) == 0 ?
           0 : -1;
}

/* Adding netbsdecho as a 13th base command (cb_register_base_programs)
 * would have pushed FIXTURE_FULL's shared baseline plus its own explicit
 * list past CB_MAX_PROGRAMS's 64-slot ceiling. Scoping yesreader/yesprobe
 * out of FIXTURE_FULL and into their own fixture frees exactly the one
 * slot that costs, rather than growing that production capacity. */
static int register_yes_probes(struct cb_kernel *kernel)
{
    return cb_kernel_register(kernel, &yesreader_program) == 0 &&
           cb_kernel_register(kernel, &yesprobe_program) == 0 ?
           0 : -1;
}

/* Whitebox reclaim contract: proves dir_close_all's node release actually
   happens on all three reclaim paths, not just that a task-owned handle
   slot disappeared (which would happen regardless, since the whole
   directories[] table dies with the task either way and would prove
   nothing about the shared, still-alive VFS node it referenced). Each
   scenario runs a real, fully scheduled kernel and inspects the shared
   "/tmp" node's RAMFS reference count -- 1 with nothing holding it open,
   plus one transient +1 for this function's own probe retain -- via the
   cb_test_ramfs_node_references whitebox hook. Scenarios 1 and 2 also
   record a "before" reading from *inside* the exiting/exec'd-from task
   itself (dir_reclaim_exit_before_references / dir_reclaim_exec_peer_
   references), captured strictly before the specific reclaim call being
   tested could have run (before exit, and as the very first action after
   the exec transition, respectively). Without that, a later, unrelated
   cleanup (api_exit firing whenever the process eventually exits, or
   task_destroy firing at kernel teardown) could silently cover for a
   missing earlier one, and a post-hoc-only check would never tell the
   difference -- exactly the gap an independent review found here. */
static void test_dir_reclaim_contract(void)
{
    struct cb_kernel *kernel;
    int status;

    /* Scenario 1: task exits without closedir(). */
    kernel = cb_kernel_create(cb_linux_host_ops());
    if (kernel == NULL)
        fail("dir reclaim kernel (exit)");
    dir_reclaim_probe_kernel = kernel;
    dir_reclaim_exit_before_references = (size_t)-1;
    dir_reclaim_exit_after_references = (size_t)-1;
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &direntreapexit_program) < 0 ||
        cb_kernel_register(kernel, &direntreapexitboot_program) < 0)
        fail("dir reclaim registration (exit)");
    /* Boots direntreapexitboot, not direntreapexit directly: a shell
       booted via cb_kernel_boot always spawns its single command and
       blocks in waitpid() for it immediately, so booting the target
       directly would let the shell's own reap (task_destroy) mask a
       missing api_exit cleanup -- see direntreapexitboot_main. */
    if (cb_kernel_boot(kernel, "direntreapexitboot") < 0)
        fail("dir reclaim boot (exit)");
    status = cb_kernel_run(kernel);
    if (status != 0)
        fail("dir reclaim run (exit)");
    /* Recorded from inside the exiting task itself, before it exited:
       must show the retain genuinely held, or the checks below would be
       vacuous -- proving nothing, since nothing was ever really open. */
    if (dir_reclaim_exit_before_references != 3)
        fail("unclosed directory unexpectedly not held open before exit");
    /* Recorded by direntreapexitboot strictly after direntreapexit's own
       api_exit ran (it is a zombie by this point) but strictly before
       anyone waitpid()s/reaps it (task_destroy has not run yet) -- this
       is what actually isolates api_exit's own cleanup. */
    if (dir_reclaim_exit_after_references != 2)
        fail("api_exit did not release an unclosed directory's node retain");
    if (dir_reclaim_check_tmp_references() != 2)
        fail("directory node reference count drifted after exit reclaim");
    cb_kernel_destroy(kernel);

    /* Scenario 2: task execs successfully without closedir() first --
       directories have no close-on-exec concept, they all close. */
    kernel = cb_kernel_create(cb_linux_host_ops());
    if (kernel == NULL)
        fail("dir reclaim kernel (exec)");
    dir_reclaim_probe_kernel = kernel;
    dir_reclaim_exec_peer_references = (size_t)-1;
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &direntreapexec_program) < 0 ||
        cb_kernel_register(kernel, &direntreapexecpeer_program) < 0)
        fail("dir reclaim registration (exec)");
    if (cb_kernel_boot(kernel, "direntreapexec") < 0)
        fail("dir reclaim boot (exec)");
    status = cb_kernel_run(kernel);
    if (status != 0)
        fail("dir reclaim run (exec)");
    /* Recorded from inside the exec'd peer, as the very first thing it
       does -- before anything else could have released the old task's
       retain (including the peer's own eventual exit). This is what
       actually attributes the release to task_finish_exec specifically,
       rather than to whatever cleanup fires whenever this process
       eventually exits regardless. */
    if (dir_reclaim_exec_peer_references != 2)
        fail("task_finish_exec did not release a directory held across exec");
    if (dir_reclaim_check_tmp_references() != 2)
        fail("directory node reference count drifted after exec reclaim");
    cb_kernel_destroy(kernel);

    /* Scenario 3: kernel teardown destroys a task that is still live and
       blocked (orphaned, never reaped), holding an open directory --
       cb_kernel_destroy calls task_destroy directly for it. This confirms
       the node is genuinely still retained at the moment of teardown (so
       the scenario is real, not vacuous) and exercises task_destroy's
       dir_close_all call under ASan/UBSan. It deliberately does not
       re-inspect the node after cb_kernel_destroy: the node may be freed
       by then, and reading it back would itself be a use-after-free bug
       in the test, not a valid check. */
    kernel = cb_kernel_create(cb_linux_host_ops());
    if (kernel == NULL)
        fail("dir reclaim kernel (destroy)");
    dir_reclaim_probe_kernel = kernel;
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &direntreapdestroyboot_program) < 0 ||
        cb_kernel_register(kernel, &direntreapdestroychild_program) < 0)
        fail("dir reclaim registration (destroy)");
    if (cb_kernel_boot(kernel, "direntreapdestroyboot") < 0)
        fail("dir reclaim boot (destroy)");
    status = cb_kernel_run(kernel);
    if (status != 0)
        fail("dir reclaim run (destroy)");
    if (dir_reclaim_check_tmp_references() != 3)
        fail("orphaned blocked child unexpectedly not still holding directory");
    cb_kernel_destroy(kernel);
}


static int execearlyretain_main(const struct cb_api_v1 *api, int argc,
                                char *const argv[], char *const envp[])
{
    char *replacement_argv[] = {(char *)"true", NULL};
    (void)argc;
    (void)argv;
    allocation_failure_countdown = 0;
    if (api->exec("true", replacement_argv, envp) >= 0)
        return 222;
    if (api->get_errno() != CB_ENOMEM)
        return 223;
    return 0;
}

static const struct cb_program_v1 execearlyretain_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "execearlyretain", 0,
    64 * 1024, execearlyretain_main
};

static void run_case(const char *command, const char *expected_output,
                     int expected_status, enum test_fixture fixture)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;
    base_allocate = host.allocate;
    base_resize = host.resize;
    host.allocate = controlled_allocate;
    host.resize = controlled_resize;
    allocation_failure_countdown = -1;
    resize_failure_countdown = -1;
    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(NULL);
    kernel = cb_kernel_create(&host);
    if (kernel == NULL)
        fail("kernel creation");
    cb_register_base_programs(kernel);
    truncate_test_kernel = kernel;
    if (fixture == FIXTURE_MAC) {
        if (register_mac_probes(kernel) != 0)
            fail("Mac probe registration");
    } else if (fixture == FIXTURE_DIRNAME) {
        if (register_dirname_probes(kernel) != 0)
            fail("dirname probe registration");
    } else if (fixture == FIXTURE_FULL) {
        if (cb_kernel_register(kernel, &cb_err_probe_program) < 0 ||
            cb_kernel_register(kernel, &cb_strcpy_probe_program) < 0 ||
            cb_kernel_register(kernel, &err_short_program) < 0 ||
            cb_kernel_register(kernel, &err_interleave_program) < 0 ||
            cb_kernel_register(kernel, &cb_memory_probe_program) < 0 ||
            cb_kernel_register(kernel, &normalpollprobe_program) < 0 ||
            cb_kernel_register(kernel, &oldpollprobe_program) < 0 ||
            cb_kernel_register(kernel, &truncateprobe_program) < 0 ||
            cb_kernel_register(kernel, &truncatechild_program) < 0 ||
            cb_kernel_register(kernel, &truncateinterleave_program) < 0 ||
            cb_kernel_register(kernel, &cb_truncate_probe_program) < 0 ||
            cb_kernel_register(kernel, &pidcheck_program) < 0 ||
            cb_kernel_register(kernel, &execprobe_program) < 0 ||
            cb_kernel_register(kernel, &unlinkprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipeallocprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipezeropeer_program) < 0 ||
            cb_kernel_register(kernel, &pipezeroprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipeedgepeer_program) < 0 ||
            cb_kernel_register(kernel, &pollwakepeer_program) < 0 ||
            cb_kernel_register(kernel, &pollwakeprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipeedgeprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipecapacitypeer_program) < 0 ||
            cb_kernel_register(kernel, &pipecapacityprobe_program) < 0 ||
            cb_kernel_register(kernel, &environpeer_program) < 0 ||
            cb_kernel_register(kernel, &environprobe_program) < 0 ||
            cb_kernel_register(kernel, &cb_exitprobe_program) < 0 ||
            cb_kernel_register(kernel, &exitwaitprobe_program) < 0 ||
            cb_kernel_register(kernel, &cb_getoptprobe_program) < 0 ||
            cb_kernel_register(kernel, &getoptwaitprobe_program) < 0 ||
            cb_kernel_register(kernel, &getopterrprobe_program) < 0 ||
            cb_kernel_register(kernel, &getoptclusterprobe_program) < 0 ||
            cb_kernel_register(kernel, &cb_errxprobe_program) < 0 ||
            cb_kernel_register(kernel, &errxprobe_program) < 0 ||
            cb_kernel_register(kernel, &terminalprobe_program) < 0 ||
            cb_kernel_register(kernel, &terminalpeer_program) < 0 ||
            cb_kernel_register(kernel, &descriptorchild_program) < 0 ||
            cb_kernel_register(kernel, &descriptorprobe_program) < 0 ||
            cb_kernel_register(kernel, &processchild_program) < 0 ||
            cb_kernel_register(kernel, &processprobe_program) < 0 ||
            cb_kernel_register(kernel, &ramfsprobe_program) < 0 ||
            cb_kernel_register(kernel, &errnochild_program) < 0 ||
            cb_kernel_register(kernel, &abiprobe_program) < 0 ||
            cb_kernel_register(kernel, &overflowprobe_program) < 0 ||
            cb_kernel_register(kernel, &execearlyretain_program) < 0 ||
            cb_kernel_register(kernel, &allocationchild_program) < 0 ||
            cb_kernel_register(kernel, &allocationafterexec_program) < 0 ||
            cb_kernel_register(kernel, &allocationexec_program) < 0 ||
            cb_kernel_register(kernel, &allocationprobe_program) < 0 ||
            cb_kernel_register(kernel, &stdioprobe_program) < 0 ||
            cb_kernel_register(kernel, &stdioepipeprobe_program) < 0 ||
            cb_kernel_register(kernel, &libcallocprobe_program) < 0)
            fail("test program registration");
    } else if (fixture == FIXTURE_DIRENT) {
        /* Scoped fixture for the dirent probes, mirroring this project's
           existing Mac-acceptance fixture split: the general fixture
           above is at CB_MAX_PROGRAMS's 64-slot ceiling, so new probe
           growth belongs in a separately scoped registration set rather
           than raising that production struct's capacity. */
        if (cb_kernel_register(kernel, &cb_direntprobe_program) < 0 ||
            cb_kernel_register(kernel, &direntbasicprobe_program) < 0 ||
            cb_kernel_register(kernel, &direntmutationprobe_program) < 0 ||
            cb_kernel_register(kernel, &direntisolationchild_program) < 0 ||
            cb_kernel_register(kernel, &direntisolationprobe_program) < 0 ||
            cb_kernel_register(kernel, &direntoldtableprobe_program) < 0 ||
            cb_kernel_register(kernel,
                               &direntopendirnulltableprobe_program) < 0 ||
            cb_kernel_register(kernel,
                               &direntreaddirnulltableprobe_program) < 0 ||
            cb_kernel_register(kernel,
                               &direntclosedirnulltableprobe_program) < 0 ||
            cb_kernel_register(kernel,
                               &direntclosedirrebindprobe_program) < 0 ||
            cb_kernel_register(kernel,
                               &direntlibcallocfailprobe_program) < 0)
            fail("dirent test program registration");
    } else if (fixture == FIXTURE_BASENAME) {
        if (register_basename_probes(kernel) != 0)
            fail("basename probe registration");
    } else if (fixture == FIXTURE_YES) {
        if (register_yes_probes(kernel) != 0)
            fail("yes probe registration");
    }
    if (cb_kernel_boot(kernel, command) < 0)
        fail("kernel boot");
    status = cb_kernel_run(kernel);
    if (status != expected_status || strcmp(captured, expected_output) != 0) {
        fprintf(stderr,
                "command: %s\nexpected status/output: %d <%s>\n"
                "actual status/output: %d <%s>\n",
                command, expected_status, expected_output, status, captured);
        exit(1);
    }
    cb_kernel_destroy(kernel);
}

static void run_interactive_case(const char *input, const char *expected_stdout,
                                 const char *expected_stderr,
                                 int expected_status)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;
    base_allocate = host.allocate;
    host.allocate = controlled_allocate;
    allocation_failure_countdown = -1;
    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(input);
    kernel = cb_kernel_create(&host);
    if (kernel == NULL)
        fail("interactive kernel creation");
    cb_register_base_programs(kernel);
    if (cb_kernel_boot(kernel, NULL) < 0)
        fail("interactive kernel boot");
    status = cb_kernel_run(kernel);
    if (status != expected_status ||
        strcmp(captured_streams[1], expected_stdout) != 0 ||
        strcmp(captured_streams[2], expected_stderr) != 0) {
        fprintf(stderr,
                "interactive expected status/stdout/stderr: %d <%s> <%s>\n"
                "interactive actual status/stdout/stderr: %d <%s> <%s>\n",
                expected_status, expected_stdout, expected_stderr, status,
                captured_streams[1], captured_streams[2]);
        exit(1);
    }
    cb_kernel_destroy(kernel);
}

static void expect_streams(const char *expected_stdout,
                           const char *expected_stderr)
{
    if (strcmp(captured_streams[1], expected_stdout) != 0 ||
        strcmp(captured_streams[2], expected_stderr) != 0) {
        fprintf(stderr,
                "expected stdout/stderr: <%s> <%s>\n"
                "actual stdout/stderr: <%s> <%s>\n",
                expected_stdout, expected_stderr, captured_streams[1],
                captured_streams[2]);
        exit(1);
    }
}

static void test_netbsd_strlen(void)
{
    static const char embedded[] = {'a', 'b', '\0', 'c', '\0'};

    if (cb_libc_strlen("") != 0 || cb_libc_strlen("cannedBSD") != 9 ||
        cb_libc_strlen(embedded) != 2 || cb_libc_strlen(embedded + 3) != 1)
        fail("NetBSD strlen semantics");
}

static void test_netbsd_strcmp(void)
{
    static const char high[] = {(char)0x80, '\0'};
    static const char low[] = {(char)0x7f, '\0'};

    if (cb_libc_strcmp("", "") != 0 || cb_libc_strcmp("same", "same") != 0 ||
        cb_libc_strcmp("a", "b") >= 0 || cb_libc_strcmp("b", "a") <= 0 ||
        cb_libc_strcmp("a", "aa") >= 0 || cb_libc_strcmp("aa", "a") <= 0 ||
        cb_libc_strcmp(high, low) <= 0)
        fail("NetBSD strcmp semantics");
}

static void test_netbsd_memcpy(void)
{
    static const unsigned char source[] = {0x10, 0x80, 0xff, 0x00, 0x55};
    unsigned char destination[8] = {0xa5, 0xa5, 0xa5, 0xa5,
                                    0xa5, 0xa5, 0xa5, 0xa5};

    if (cb_libc_memcpy(destination, source, 0) != destination ||
        destination[0] != 0xa5 ||
        cb_libc_memcpy(destination + 1, source, sizeof(source)) !=
            destination + 1 ||
        memcmp(destination + 1, source, sizeof(source)) != 0 ||
        destination[0] != 0xa5 || destination[6] != 0xa5)
        fail("NetBSD memcpy semantics");
}

static void test_netbsd_memmove(void)
{
    unsigned char backward[] = "abcdef";
    unsigned char forward[] = "abcdef";

    if (cb_libc_memmove(backward, backward, 0) != backward ||
        cb_libc_memmove(backward + 2, backward, 4) != backward + 2 ||
        memcmp(backward, "ababcd", 6) != 0 ||
        cb_libc_memmove(forward, forward + 2, 4) != forward ||
        memcmp(forward, "cdefef", 6) != 0)
        fail("NetBSD memmove overlap semantics");
}

static void test_netbsd_memcmp(void)
{
    static const unsigned char equal[] = {0x00, 0x7f, 0x80, 0xff};
    static const unsigned char lower[] = {0x00, 0x7f, 0x80, 0x00};

    if (cb_libc_memcmp(equal, lower, 0) != 0 ||
        cb_libc_memcmp(equal, equal, sizeof(equal)) != 0 ||
        cb_libc_memcmp(lower, equal, sizeof(equal)) >= 0 ||
        cb_libc_memcmp(equal, lower, sizeof(equal)) <= 0)
        fail("NetBSD memcmp semantics");
}

static void test_netbsd_strchr(void)
{
    char text[] = "abca";

    if (cb_libc_strchr(text, 'a') != text ||
        cb_libc_strchr(text, 'c') != text + 2 ||
        cb_libc_strchr(text, '\0') != text + 4 ||
        cb_libc_strchr(text, 'z') != NULL ||
        cb_libc_strchr(text, 0x161) != text + 0)
        fail("NetBSD strchr semantics");
}


static void test_nullboot(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;

    if (cb_kernel_boot(NULL, "sh") != -1)
        fail("null kernel boot did not return -1");

    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console("exit\n");

    kernel = cb_kernel_create(&host);
    if (kernel == NULL) fail("kernel creation");
    cb_register_base_programs(kernel);
    if (cb_kernel_boot(kernel, NULL) < 0)
        fail("null boot rejected");
    if (cb_kernel_run(kernel) != 0)
        fail("null boot run");
    cb_kernel_destroy(kernel);
}

static void test_vfs_executable_nodes(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    extern const struct cb_program_v1 cb_shell_program;
    struct cb_stat_v1 st;
    struct cb_task task;

    if (kernel == NULL)
        fail("test kernel creation");
    if (cb_kernel_register(kernel, &cb_vfs_executable_probe_program) < 0)
        fail("register test_vfs_exec");
    if (cb_kernel_register(kernel, &cb_shell_program) < 0)
        fail("register shell");

    memset(&task, 0, sizeof(task));
    task.kernel = kernel;
    task.root = kernel->vfs_root;
    task.cwd = kernel->vfs_root;

    if (cb_vfs_stat_path(&task, "/bin/vfsexecprobe", &st) != 0)
        fail("stat executable failed");
    if (st.type != CB_NODE_EXECUTABLE || st.size != 0)
        fail("stat executable metadata wrong");

    if (cb_kernel_boot(kernel, "vfsexecprobe") != 0)
        fail("boot failed");
    int status = cb_kernel_run(kernel);
    if (status != 0) {
        fprintf(stderr, "run failed with %d\n", status);
        fail("run failed");
    }

    if (cb_vfs_stat_path(&task, "/missing/sh", &st) == 0)
        fail("stat missing succeeded");

    cb_kernel_destroy(kernel);
}

static void test_vfs_mount_routing(void)
{
    struct cb_kernel kernel;
    struct cb_task task;
    struct cb_vfs_mount *mount2;
    struct cb_vfs_node *root2;
    struct cb_vfs_node *found = NULL;
    struct cb_vfs_node *mnt_node = NULL;
    struct cb_kernel wrong_kernel;
    struct cb_open_file *file;
    struct cb_stat_v1 st;
    char buffer[256];

    memset(&kernel, 0, sizeof(kernel));
    memset(&task, 0, sizeof(task));
    kernel.host = cb_linux_host_ops();

    if (cb_vfs_initialize(&kernel) < 0)
        fail("VFS initialization");

    task.kernel = &kernel;
    task.root = kernel.vfs_root;
    task.cwd = kernel.vfs_root;
    cb_vfs_node_retain(task.root);
    cb_vfs_node_retain(task.cwd);
    task.error_cell = cb_allocate(&kernel, sizeof(int));

    /* Make a directory /mnt in root filesystem */
    if (cb_vfs_mkdir_path(&task, "/mnt", 0777) < 0)
        fail("mkdir /mnt failed");

    /* Mount a second RAMFS at /mnt */
    mount2 = cb_ramfs_mount_create(&kernel);
    if (mount2 == NULL)
        fail("second RAMFS creation");
    if (cb_vfs_mount_path(&task, "/mnt", mount2) < 0)
        fail("mount /mnt failed");

    /* Create a file in the second mount */
    if (cb_vfs_mkdir_path(&task, "/mnt/hello", 0777) < 0)
        fail("mkdir /mnt/hello failed");

    /* 1. Both-filesystem isolation */
    root2 = mount2->ops->root(mount2);
    if (root2->ops->lookup(root2, "hello", 5, &found) != 0)
        fail("cross-mount mkdir did not route to the second mount");

    if (kernel.vfs_root->ops->lookup(kernel.vfs_root, "mnt", 3, &mnt_node) != 0)
        fail("root /mnt disappeared");

    if (mnt_node->ops->lookup(mnt_node, "hello", 5, &found) == 0)
        fail("isolation failed: cross-mount node leaked into underlying mount point");

    /* 2. Lookup/cwd/stat/open and .. at the mount root */
    if (cb_vfs_chdir_path(&task, "/mnt") < 0)
        fail("chdir /mnt failed");
    if (cb_vfs_getcwd_path(&task, buffer, sizeof(buffer)) == NULL || strcmp(buffer, "/mnt") != 0)
        fail("getcwd in mount point");
    if (cb_vfs_chdir_path(&task, "..") < 0)
        fail("chdir .. from mount point failed");
    if (cb_vfs_getcwd_path(&task, buffer, sizeof(buffer)) == NULL || strcmp(buffer, "/") != 0)
        fail("getcwd after .. from mount point");

    if (cb_vfs_stat_path(&task, "/mnt", &st) < 0)
        fail("stat /mnt failed");
    if (st.type != CB_NODE_DIRECTORY)
        fail("stat /mnt not a directory");

    file = cb_vfs_open(&task, "/mnt", 0, 0);
    if (file != NULL)
        fail("open /mnt succeeded (expected EISDIR)");
    if (*task.error_cell != CB_EISDIR)
        fail("open /mnt did not return EISDIR");

    /* 3. Exact EPERM on mount-point unlink */
    if (cb_vfs_unlink_path(&task, "/mnt") >= 0)
        fail("unlinking a mount point succeeded");
    if (*task.error_cell != CB_EPERM)
        fail("unlinking a mount point did not return EPERM");

    /* 4. duplicate/invalid/wrong-kernel/capacity failures */
    /* duplicate */
    struct cb_vfs_mount *extra = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mount_path(&task, "/mnt", extra) != -CB_EEXIST)
        fail("duplicate mount did not return EEXIST");
    extra->ops->destroy(extra);

    /* invalid */
    if (cb_vfs_mount_path(NULL, "/mnt", extra) != -CB_EINVAL)
        fail("invalid task did not return EINVAL");

    /* wrong-kernel */
    memset(&wrong_kernel, 0, sizeof(wrong_kernel));
    wrong_kernel.host = cb_linux_host_ops();
    struct cb_vfs_mount *wrong_mount = cb_ramfs_mount_create(&wrong_kernel);
    if (cb_vfs_mount_path(&task, "/mnt/hello", wrong_mount) != -CB_EINVAL)
        fail("wrong kernel mount did not return EINVAL");
    wrong_mount->ops->destroy(wrong_mount);


    /* Test duplicates and root path */
    struct cb_vfs_mount *dup_mount = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mount_path(&task, "/mnt", dup_mount) != -CB_EEXIST)
        fail("duplicate mount target not rejected");
    if (cb_vfs_mount_path(&task, "/mnt/", dup_mount) != -CB_EEXIST)
        fail("duplicate routed target not rejected");
    if (cb_vfs_mount_path(&task, "/", dup_mount) != -CB_EINVAL)
        fail("root overlay not rejected");
    dup_mount->ops->destroy(dup_mount);

    struct cb_vfs_mount *reused = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mkdir_path(&task, "/reused", 0777) < 0) fail("mkdir /reused");
    if (cb_vfs_mount_path(&task, "/reused", kernel.root_mount) != -CB_EINVAL)
        fail("root mount object reused");

    if (cb_vfs_mount_path(&task, "/reused", task.kernel->mounts[0].mount) != -CB_EINVAL)
        fail("existing mount object reused");

    reused->ops->destroy(reused);

    /* Test file overlay (not a directory) */
    struct cb_vfs_mount *file_mount = cb_ramfs_mount_create(&kernel);
    struct cb_open_file *tmp_file = cb_vfs_open(&task, "/reused/file", CB_O_CREAT | CB_O_WRONLY, 0666);
    if (tmp_file == NULL)
        fail("file create failed");
    cb_open_file_release(tmp_file);
    if (cb_vfs_mount_path(&task, "/reused/file", file_mount) != -CB_ENOTDIR)
        fail("mounting over file not ENOTDIR");
    file_mount->ops->destroy(file_mount);

    /* Test malformed candidate root */
    struct cb_vfs_mount malformed_mount;
    malformed_mount.kernel = &kernel;
    malformed_mount.ops = NULL;
    if (cb_vfs_mount_path(&task, "/reused", &malformed_mount) != -CB_EINVAL)
        fail("missing mount ops not rejected");

    struct cb_vfs_mount_ops malformed_ops;
    malformed_ops.abi_version = CB_ABI_VERSION_V1;
    malformed_ops.struct_size = sizeof(malformed_ops);
    malformed_ops.root = NULL; /* NULL root */
    malformed_ops.destroy = NULL;
    malformed_mount.ops = &malformed_ops;
    if (cb_vfs_mount_path(&task, "/reused", &malformed_mount) != -CB_EINVAL)
        fail("invalid mount ops not rejected");

    /* capacity */
    struct cb_vfs_mount *m3 = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mkdir_path(&task, "/mnt3", 0777) < 0 || cb_vfs_mount_path(&task, "/mnt3", m3) < 0)
        fail("mount 3 failed");
    struct cb_vfs_mount *m4 = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mkdir_path(&task, "/mnt3/nested", 0777) < 0 || cb_vfs_mount_path(&task, "/mnt3/nested", m4) < 0)
        fail("mount 4 (nested) failed");
    struct cb_vfs_mount *m5 = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mkdir_path(&task, "/mnt5", 0777) < 0 || cb_vfs_mount_path(&task, "/mnt5", m5) < 0)
        fail("mount 5 failed");

    /* Should now be at capacity (4 extra mounts). */
    struct cb_vfs_mount *m6 = cb_ramfs_mount_create(&kernel);
    if (cb_vfs_mount_path(&task, "/mnt/hello", m6) != -CB_ENOMEM)
        fail("capacity limit did not return ENOMEM");
    m6->ops->destroy(m6);

    /* 5. retain/release plus failed-install cleanup */
    /* Check error on nonexistent path without leaking retain */
    struct cb_vfs_mount *m7 = cb_ramfs_mount_create(&kernel);
    kernel.mount_count--; /* temp decrement */
    if (cb_vfs_mount_path(&task, "/nonexistent", m7) != -CB_ENOENT)
        fail("mounting on nonexistent path failed");
    kernel.mount_count++;
    m7->ops->destroy(m7);

    cb_release(&kernel, task.error_cell);
    cb_vfs_node_release(task.cwd);
    cb_vfs_node_release(task.root);
    cb_vfs_destroy(&kernel);
}

static void test_err(void)
{
    run_case("errinterleave", "", 0, 1);
    run_case("libcerrprobe", "libcerrprobe: path alpha %: no such file or directory\n", 7, 1);
    expect_streams("", "libcerrprobe: path alpha %: no such file or directory\n");
    run_case("libcerrprobe null", "libcerrprobe: no such file or directory\n", 7, 1);
    run_case("libcerrprobe empty", "libcerrprobe: : no such file or directory\n", 7, 1);
    run_case("libcerrprobe closed", "", 7, 1);
    /* A zero-progress writer must not trap err in an infinite retry loop. */
    capture_write_limit = 0;
    run_case("libcerrprobe", "", 7, 1);
    capture_write_limit = (size_t)-1;
}

static void test_mac_acceptance(void)
{
#define CB_MAC_CASE(command, expected, status) \
    run_case(command, expected, status, FIXTURE_MAC);
#include "../platform/mac68k/acceptance_cases.def"
#undef CB_MAC_CASE
}

static uint64_t mock_clock_time = 1000;


static int console_poll_negative_one_seen = 0;
static int clockloss_trigger = 0;
static struct cb_kernel *clockloss_kernel = NULL;

static uint64_t clockloss_monotonic(void)
{
    if (clockloss_trigger) return 0;
    return mock_clock_time;
}

static int clockloss_console_poll(int timeout)
{
    struct cb_task *pt;
    int has_finite = 0;

    if (timeout < 0)
        console_poll_negative_one_seen = 1;

    if (clockloss_kernel != NULL) {
        for (pt = clockloss_kernel->tasks; pt != NULL; pt = pt->next) {
            if (pt->state == CB_TASK_BLOCKED_POLL && pt->wake_timeout > 0)
                has_finite = 1;
        }
    }

    if (has_finite && clockloss_trigger == 0) {
        clockloss_trigger = 1;
    }
    return 0;
}

static void test_poll_clockloss(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;


    console_poll_negative_one_seen = 0;
    host.monotonic_millis = clockloss_monotonic;
    host.console_poll = clockloss_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(NULL);

    kernel = cb_kernel_create(&host);
    if (!kernel) fail("clockloss kernel");
    clockloss_kernel = kernel;
    clockloss_trigger = 0;
    clockloss_kernel = kernel;
    clockloss_trigger = 0;
    cb_register_base_programs(kernel);
    cb_kernel_register(kernel, &clocklossprobe_program);

    if (cb_kernel_boot(kernel, "clocklossprobe") < 0) fail("clockloss boot");
    status = cb_kernel_run(kernel);
    if (status != 0) fail("clockloss failed");
    if (console_poll_negative_one_seen) fail("clockloss passed -1 to console_poll");
    cb_kernel_destroy(kernel);
    printf("clockloss test passed\n");
}

static uint64_t advancing_clock_monotonic(void)
{
    mock_clock_time += 1;
    return mock_clock_time;
}

static void test_poll_runnable_timeout(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    struct cb_kernel *kernel;
    int status;

    mock_clock_time = 1000;

    host.monotonic_millis = advancing_clock_monotonic;
    host.console_poll = controlled_console_poll;
    host.console_read = controlled_console_read;
    host.console_write = capture_write;
    reset_console(NULL);
    console_poll_ready = 0;

    kernel = cb_kernel_create(&host);
    if (!kernel) fail("runnabletimeout kernel");
    cb_register_base_programs(kernel);
    cb_kernel_register(kernel, &runnabletimeoutprobe_program);
    cb_kernel_register(kernel, &yieldingspinner_program);

    if (cb_kernel_boot(kernel, "runnabletimeoutprobe") < 0) fail("runnabletimeout boot");
    status = cb_kernel_run(kernel);
    if (status != 0) { printf("runnabletimeout status: %d (0x%x)\n", status, status); fail("runnabletimeout failed"); }
    cb_kernel_destroy(kernel);
    printf("runnable timeout test passed\n");
}

void cb_test_stdio_state(void);
void cb_test_argv(void);
void cb_test_echo_state(void);
void cb_test_getopt_arg(void);
void cb_test_locale(void);
void cb_test_terminal(void);

static int startup_identity_main(const struct cb_api_v1 *api, int argc,
                                 char *const argv[], char *const envp[])
{
    char **mutable_argv = (char **)argv;
    char *original;
    const char *identity;
    int preserved;
    (void)envp;
    if (argc < 1 || argv[0] == NULL)
        return 2;
    original = argv[0];
    identity = api->getprogname();
    mutable_argv[0] = (char *)"replacement";
    preserved = api->getprogname() == identity &&
                strcmp(api->getprogname(), original) == 0;
    mutable_argv[0] = original;
    return preserved ? 0 : 3;
}

static const struct cb_program_v1 startup_identity_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "startupidentity", 0,
    64 * 1024, startup_identity_main
};

static void test_startup_identity(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    int status;
    if (kernel == NULL)
        fail("startup identity kernel");
    cb_register_base_programs(kernel);
    if (cb_kernel_register(kernel, &startup_identity_program) < 0 ||
        cb_kernel_boot(kernel, "startupidentity") < 0)
        fail("startup identity setup");
    status = cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
    if (status != 0)
        fail("argv vector replacement renamed startup identity");
}

int main(int argc, char **argv)
{
    test_startup_identity();
    if (argc == 2 && strcmp(argv[1], "--getopt-arg") == 0) {
        cb_test_getopt_arg();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--argv") == 0) {
        cb_test_argv();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--stdio-state") == 0) {
        cb_test_stdio_state();
        puts("stdio state tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--echo-state") == 0) {
        cb_test_echo_state();
        puts("echo state tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--progname") == 0) {
        run_case("libcprognameprobe", "", 0, FIXTURE_MAC);
        puts("program-name tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--err") == 0) {
        test_err();
        puts("err diagnostic tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--mac-acceptance") == 0) {
        test_mac_acceptance();
        puts("Mac acceptance command probes passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--locale") == 0) {
        cb_test_locale();
        puts("locale tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--terminal") == 0) {
        cb_test_terminal();
        puts("terminal tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--poll") == 0) {
        run_case("pollwakeprobe", "", 0, 1);
        run_case("normalpollprobe", "", 0, 1);
        run_case("oldpollprobe", "", 0, 1);
        test_poll_clockloss();
        test_poll_runnable_timeout();
        puts("poll tests passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--truncate") == 0) {
        test_truncate_vfs_contract();
        run_case("libctruncateprobe", "", 0, 1);
        run_case("truncateprobe", "", 0, 1);
        run_case("truncateinterleave", "", 0, 1);
        puts("truncate tests passed");
        return 0;
    }
    if (argc != 1)
        fail("unknown test selection");
    cb_test_getopt_arg();
    cb_test_argv();
    cb_test_stdio_state();
    cb_test_echo_state();
    test_mac_acceptance();
    test_err();
    test_netbsd_strlen();
    test_netbsd_strcmp();
    test_netbsd_memcpy();
    test_netbsd_memmove();
    test_netbsd_memcmp();
    test_netbsd_strchr();
    test_host_contract();
    test_vfs_contract();
    test_truncate_vfs_contract();
    test_dir_reclaim_contract();
    test_registration_contract();
    test_exec_early_retain();
    test_executor_contract();
    test_allocation_cleanup();
    test_uninitialized_host_memory();
    test_nullboot();
    test_vfs_executable_nodes();
    test_vfs_mount_routing();
    expect_path("/", "/", "/");
    expect_path("/home/user", "../user/./file", "/home/user/file");
    expect_path("/tmp", "../../../../x", "/x");
    expect_path("/", "//tmp///x", "/tmp/x");

    run_case("echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result",
             "HELLO\n", 0, 0);
    run_case("false; echo $?", "1\n", 0, 0);
    run_case("echo abc | cat | tr a-z A-Z", "ABC\n", 0, 0);
    run_case("echo one > /tmp/x; echo two >> /tmp/x; cat /tmp/x",
             "one\ntwo\n", 0, 0);
    run_case("cd /tmp; pwd", "/tmp\n", 0, 0);
    run_case("echo -n hello | wc -c", "5\n", 0, 0);
    run_case("echo -n | wc -c", "0\n", 0, 0);
    run_case("echo -n sixsix > /tmp/wc; wc -c /tmp/wc", "6\n", 0, 0);
    run_case("wc -c /missing",
             "wc: /missing: no such file or directory\n", 1, 0);
    expect_streams("", "wc: /missing: no such file or directory\n");
    run_case("wc", "usage: wc -c [file]\n", 2, 0);
    expect_streams("", "usage: wc -c [file]\n");
    run_case("export WORD=works; echo $WORD", "works\n", 0, 0);
    run_case("echo input > /tmp/in; cat < /tmp/in", "input\n", 0, 0);
    run_case("echo '' \"\" a\\ b 'c d' \"e f\" ';' '|'",
             "  a b c d e f ; |\n", 0, 0);
    run_case("export V=world; echo '$V' \"$V\" \\$V \"x${V}y\"",
             "$V world $V xworldy\n", 0, 0);
    run_case("export V=world; unset V; echo x${V}y", "xy\n", 0, 0);
    run_case("export GOOD=ok ALSO=fine; echo $GOOD-$ALSO; "
             "unset GOOD ALSO; echo x$GOOD$ALSO", "ok-fine\nx\n", 0, 0);
    run_case("false; echo $?; true; echo $?", "1\n0\n", 0, 0);
    run_case("echo 'a;b'; echo \"c;d\"", "a;b\nc;d\n", 0, 0);
    run_case("echo a\\;b a\\|b a\\>b", "a;b a|b a>b\n", 0, 0);
    run_case("cd /tmp; pwd; cd; pwd", "/tmp\n/home/user\n", 0, 0);
    run_case("cd /missing; echo $?",
             "sh: cd: no such file or directory\n1\n", 0, 0);
    expect_streams("1\n", "sh: cd: no such file or directory\n");
    run_case("cd / /tmp; echo $?",
             "sh: cd: too many arguments\n2\n", 0, 0);
    expect_streams("2\n", "sh: cd: too many arguments\n");
    run_case("pwd > /tmp/pwd; cat /tmp/pwd", "/\n", 0, 0);
    run_case("echo abc > /tmp/in; tr a-z A-Z < /tmp/in > /tmp/out; "
             "cat /tmp/out", "ABC\n", 0, 0);
    run_case("echo input | cat -", "input\n", 0, 0);
    run_case("echo abc | tr a-c x-z", "xyz\n", 0, 0);
    run_case("echo -n compact", "compact", 0, 0);
    run_case("export 1BAD=value; echo $?",
             "sh: export: invalid name\n2\n", 0, 0);
    expect_streams("2\n", "sh: export: invalid name\n");
    run_case("unset BAD-NAME; echo $?",
             "sh: unset: invalid name\n2\n", 0, 0);
    expect_streams("2\n", "sh: unset: invalid name\n");
    run_case("echo ${BAD-NAME}", "sh: syntax error\n", 2, 0);
    expect_streams("", "sh: syntax error\n");
    run_case("echo ${}", "sh: syntax error\n", 2, 0);
    expect_streams("", "sh: syntax error\n");
    run_case("exit nope", "sh: exit: numeric argument required\n", 2, 0);
    expect_streams("", "sh: exit: numeric argument required\n");
    run_case("pwd | cat", "/\n", 0, 0);
    run_case("cd /tmp | cat; pwd", "/\n", 0, 0);
    run_case("export PIPEVAR=child | cat; echo x${PIPEVAR}x", "xx\n", 0, 0);
    run_case("true | false; echo $?; false | true; echo $?", "1\n0\n", 0, 0);
    run_case("exit 7 | cat; echo $?", "0\n", 0, 0);
    run_case("echo hi | missing-command | cat; echo $?",
             "sh: missing-command: no such file or directory\n127\n", 0, 0);
    expect_streams("127\n",
                   "sh: missing-command: no such file or directory\n");
    run_case("echo unterminated'", "sh: syntax error\n", 2, 0);
    expect_streams("", "sh: syntax error\n");
    run_case("echo trailing\\", "sh: syntax error\n", 2, 0);
    expect_streams("", "sh: syntax error\n");
    run_case("echo hi |", "sh: missing command\n", 2, 0);
    expect_streams("", "sh: missing command\n");
    run_case("echo hi >", "sh: redirection requires a path\n", 2, 0);
    expect_streams("", "sh: redirection requires a path\n");
    run_case("echo no > /missing/file; echo $?",
             "sh: no such file or directory\n1\n", 0, 0);
    expect_streams("1\n", "sh: no such file or directory\n");
    run_case("cat /missing; echo $?",
             "cat: /missing: no such file or directory\n1\n", 0, 0);
    expect_streams("1\n", "cat: /missing: no such file or directory\n");
    run_case("echo abc | tr z-a A-Z", "usage: tr string1 string2\n", 2, 0);
    expect_streams("", "usage: tr string1 string2\n");
    run_case("exit 257", "", 1, 0);
    run_case("exit -1", "", 255, 0);
    run_case("exit 1 2; echo continued",
             "sh: exit: too many arguments\ncontinued\n", 0, 0);
    expect_streams("continued\n", "sh: exit: too many arguments\n");
    run_case("false; exit", "", 1, 0);
    run_case("execprobe", "", 7, 1);
    run_case("unlinkprobe", "", 0, 1);
    run_case("pipeallocprobe", "", 0, 1);
    run_case("pipezeroprobe", "", 0, 1);
    run_case("pipeedgeprobe", "", 0, 1);
    run_case("pollwakeprobe", "", 0, 1);
    run_case("pipecapacityprobe", "", 0, 1);
    run_case("environprobe", "", 0, 1);
    run_case("exitwaitprobe", "", 0, 1);
    run_case("getoptwaitprobe", "libcgetoptprobe: illegal option -- x\n", 0, 1);
    run_case("getopterrprobe", "", 0, 1);
    run_case("getoptclusterprobe", "", 0, 1);
    run_case("errxprobe", "", 0, 1);
    run_case("strcpyprobe", "", 0, 1);
    run_case("libcdirentprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntbasicprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntmutationprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntisolationprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntoldtableprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntopendirnulltableprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntreaddirnulltableprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntclosedirnulltableprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntclosedirrebindprobe", "", 0, FIXTURE_DIRENT);
    run_case("direntlibcallocfailprobe", "", 0, FIXTURE_DIRENT);
    run_case("terminalprobe", "", 0, 1);
    run_case("descriptorprobe", "", 0, 1);
    run_case("processprobe", "", 0, 1);
    run_case("libctruncateprobe", "", 0, 1);
    cb_test_locale();
    cb_test_terminal();
    run_case("normalpollprobe", "", 0, 1);
    run_case("oldpollprobe", "", 0, 1);
    test_poll_clockloss();
    test_poll_runnable_timeout();
    run_case("truncateprobe", "", 0, 1);
    run_case("truncateinterleave", "", 0, 1);
    run_case("libcdirnameprobe", "", 0, FIXTURE_DIRNAME);
    run_case("dirnameoldtableprobe", "", 0, FIXTURE_DIRNAME);
    run_case("dirnamenulltableprobe", "", 0, FIXTURE_DIRNAME);
    run_case("dirnameisolationprobe", "", 0, FIXTURE_DIRNAME);
    run_case("libcbasenameprobe", "", 0, FIXTURE_BASENAME);
    run_case("basenameoldtableprobe", "", 0, FIXTURE_BASENAME);
    run_case("basenamenulltableprobe", "", 0, FIXTURE_BASENAME);
    run_case("basenameisolationprobe", "", 0, FIXTURE_BASENAME);
    run_case("ramfsprobe", "", 0, 1);
    run_case("abiprobe", "", 0, 1);
    run_case("execearlyretain", "", 0, 1);
    run_case("overflowprobe", "", 0, 1);
    run_case("allocationprobe", "", 0, 1);
    capture_write_limit = 2;
    run_case("stdioprobe", "out:value:%/(null)\nerr:bad\n", 0, 1);
    expect_streams("out:value:%/(null)\n", "err:bad\n");
    capture_write_limit = (size_t)-1;
    run_case("stdioprobe error", "", 0, 1);
    run_case("stdioepipeprobe", "", 0, 1);
    run_case("stdioprobe unsupported", "prefix:", 0, 1);
    run_case("libcallocprobe", "", 0, 1);
    run_case("yes ok | yesreader", "ok\n", 0, FIXTURE_YES);
    run_case("yesprobe", "ok\n", 0, FIXTURE_YES);
    run_case("missing-command", "sh: missing-command: no such file or directory\n",
             127, 0);
    if (captured_streams[1][0] != '\0' ||
        strcmp(captured_streams[2],
               "sh: missing-command: no such file or directory\n") != 0)
        fail("stdout/stderr separation");
    run_interactive_case("echo hello\nexit 3\n",
                         "cannedBSD$ hello\ncannedBSD$ ", "", 3);
    puts("all core tests passed");
    return 0;
}
