#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char captured[32768];
static size_t captured_size;
static char captured_streams[3][32768];
static size_t captured_stream_sizes[3];
static void *(*base_allocate)(size_t);
static int allocation_failure_countdown = -1;
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
    if (api->close(descriptors[0]) < 0)
        return 72;
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

static void run_case(const char *command, const char *expected_output,
                     int expected_status, int register_test_programs)
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
            cb_kernel_register(kernel, &terminalprobe_program) < 0 ||
            cb_kernel_register(kernel, &terminalpeer_program) < 0 ||
            cb_kernel_register(kernel, &descriptorchild_program) < 0 ||
            cb_kernel_register(kernel, &descriptorprobe_program) < 0 ||
            cb_kernel_register(kernel, &processchild_program) < 0 ||
            cb_kernel_register(kernel, &processprobe_program) < 0 ||
            cb_kernel_register(kernel, &ramfsprobe_program) < 0)
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

int main(void)
{
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
    run_case("export WORD=works; echo $WORD", "works\n", 0, 0);
    run_case("echo input > /tmp/in; cat < /tmp/in", "input\n", 0, 0);
    run_case("execprobe", "", 7, 1);
    run_case("unlinkprobe", "", 0, 1);
    run_case("pipeallocprobe", "", 0, 1);
    run_case("pipezeroprobe", "", 0, 1);
    run_case("pipeedgeprobe", "", 0, 1);
    run_case("pipecapacityprobe", "", 0, 1);
    run_case("terminalprobe", "", 0, 1);
    run_case("descriptorprobe", "", 0, 1);
    run_case("processprobe", "", 0, 1);
    run_case("ramfsprobe", "", 0, 1);
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
