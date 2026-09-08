#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char captured[32768];
static size_t captured_size;
static char captured_streams[3][32768];
static size_t captured_stream_sizes[3];
static void *(*base_allocate)(size_t);
static void *(*base_resize)(void *, size_t);
static void (*base_release)(void *);
static int allocation_failure_countdown = -1;
static int resize_failure_countdown = -1;
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

static void expect_invalid_host(const struct cb_host_ops_v1 *host,
                                const char *field)
{
    struct cb_kernel *kernel = cb_kernel_create(host);
    if (kernel != NULL) {
        cb_kernel_destroy(kernel);
        fprintf(stderr, "host with invalid %s was accepted\n", field);
        exit(1);
    }
}

static void test_host_contract(void)
{
    const struct cb_host_ops_v1 *linux_host = cb_linux_host_ops();
    struct cb_host_ops_v1 host;
    uint64_t before;
    uint64_t after;

    if (linux_host == NULL ||
        linux_host->abi_version != CB_ABI_VERSION_V1 ||
        linux_host->struct_size != sizeof(*linux_host) ||
        linux_host->allocate == NULL || linux_host->resize == NULL ||
        linux_host->release == NULL || linux_host->context_root == NULL ||
        linux_host->context_create == NULL ||
        linux_host->context_switch == NULL ||
        linux_host->context_destroy == NULL ||
        linux_host->console_poll == NULL ||
        linux_host->console_read == NULL ||
        linux_host->console_write == NULL ||
        linux_host->monotonic_millis == NULL ||
        linux_host->wall_clock_millis == NULL ||
        linux_host->yield_host == NULL || linux_host->fatal == NULL)
        fail("Linux host operation table");
    before = linux_host->monotonic_millis();
    linux_host->yield_host();
    after = linux_host->monotonic_millis();
    if (before == 0 || after < before || linux_host->wall_clock_millis() == 0)
        fail("Linux host clocks");

    expect_invalid_host(NULL, "null table");
    host = *linux_host;
    host.abi_version = 0;
    expect_invalid_host(&host, "version");
    host = *linux_host;
    host.struct_size = sizeof(host) - 1;
    expect_invalid_host(&host, "size");
#define EXPECT_NULL_HOST_CALLBACK(member) do { \
    host = *linux_host; \
    host.member = NULL; \
    expect_invalid_host(&host, #member); \
} while (0)
    EXPECT_NULL_HOST_CALLBACK(allocate);
    EXPECT_NULL_HOST_CALLBACK(resize);
    EXPECT_NULL_HOST_CALLBACK(release);
    EXPECT_NULL_HOST_CALLBACK(context_root);
    EXPECT_NULL_HOST_CALLBACK(context_create);
    EXPECT_NULL_HOST_CALLBACK(context_switch);
    EXPECT_NULL_HOST_CALLBACK(context_destroy);
    EXPECT_NULL_HOST_CALLBACK(console_poll);
    EXPECT_NULL_HOST_CALLBACK(console_read);
    EXPECT_NULL_HOST_CALLBACK(console_write);
    EXPECT_NULL_HOST_CALLBACK(monotonic_millis);
    EXPECT_NULL_HOST_CALLBACK(wall_clock_millis);
    EXPECT_NULL_HOST_CALLBACK(yield_host);
    EXPECT_NULL_HOST_CALLBACK(fatal);
#undef EXPECT_NULL_HOST_CALLBACK
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
    node_copy.struct_size = sizeof(node_copy) - 1;
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

    root->ops = node_ops;
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
    cb_vfs_destroy(&kernel);
    if (kernel.root_mount != NULL || kernel.vfs_root != NULL)
        fail("root mount destruction");
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
        CB_POLL_READ)
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
        CB_POLL_WRITE)
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
        0, CB_EPERM, CB_ENOENT, CB_EINTR, CB_EIO, CB_EBADF, CB_ECHILD,
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
        api->environ_location == NULL)
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

static void run_case(const char *command, const char *expected_output,
                     int expected_status, int register_test_programs)
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
    if (register_test_programs) {
        if (cb_kernel_register(kernel, &pidcheck_program) < 0 ||
            cb_kernel_register(kernel, &execprobe_program) < 0 ||
            cb_kernel_register(kernel, &unlinkprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipeallocprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipezeropeer_program) < 0 ||
            cb_kernel_register(kernel, &pipezeroprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipeedgepeer_program) < 0 ||
            cb_kernel_register(kernel, &pipeedgeprobe_program) < 0 ||
            cb_kernel_register(kernel, &pipecapacitypeer_program) < 0 ||
            cb_kernel_register(kernel, &pipecapacityprobe_program) < 0 ||
            cb_kernel_register(kernel, &environpeer_program) < 0 ||
            cb_kernel_register(kernel, &environprobe_program) < 0 ||
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
            cb_kernel_register(kernel, &allocationchild_program) < 0 ||
            cb_kernel_register(kernel, &allocationafterexec_program) < 0 ||
            cb_kernel_register(kernel, &allocationexec_program) < 0 ||
            cb_kernel_register(kernel, &allocationprobe_program) < 0 ||
            cb_kernel_register(kernel, &libcallocprobe_program) < 0 ||
            cb_kernel_register(kernel, &yesreader_program) < 0 ||
            cb_kernel_register(kernel, &yesprobe_program) < 0)
            fail("test program registration");
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

int main(void)
{
    test_netbsd_strlen();
    test_netbsd_strcmp();
    test_netbsd_memcpy();
    test_netbsd_memmove();
    test_netbsd_memcmp();
    test_netbsd_strchr();
    test_host_contract();
    test_vfs_contract();
    test_registration_contract();
    test_executor_contract();
    test_allocation_cleanup();
    test_uninitialized_host_memory();
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
    run_case("pipecapacityprobe", "", 0, 1);
    run_case("environprobe", "", 0, 1);
    run_case("terminalprobe", "", 0, 1);
    run_case("descriptorprobe", "", 0, 1);
    run_case("processprobe", "", 0, 1);
    run_case("ramfsprobe", "", 0, 1);
    run_case("abiprobe", "", 0, 1);
    run_case("overflowprobe", "", 0, 1);
    run_case("allocationprobe", "", 0, 1);
    run_case("libcallocprobe", "", 0, 1);
    run_case("yes ok | yesreader", "ok\n", 0, 1);
    run_case("yesprobe", "ok\n", 0, 1);
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
