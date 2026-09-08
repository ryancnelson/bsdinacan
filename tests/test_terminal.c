#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct cb_program_v1 cb_terminal_probe_program;
extern int terminal_optional_main(int argc, char **argv);
static struct cb_kernel *test_kernel;
static int input_reads, input_polls, output_writes;
static size_t input_position;
static const char input_bytes[] = "a\r\b\004\n";

static int input_poll(int timeout)
{
    (void)timeout;
    ++input_polls;
    return 1;
}
static cb_ssize_t input_read(void *buffer, size_t count)
{
    ++input_reads;
    if (count > sizeof(input_bytes) - 1 - input_position)
        count = sizeof(input_bytes) - 1 - input_position;
    memcpy(buffer, input_bytes + input_position, count);
    input_position += count;
    return (cb_ssize_t)count;
}
static cb_ssize_t output_write(int stream, const void *buffer, size_t count)
{
    (void)stream;
    (void)buffer;
    ++output_writes;
    return (cb_ssize_t)count;
}

static int inherited_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    int fd;
    (void)argc; (void)argv; (void)envp;
    for (fd = 0; fd < 3; ++fd) {
        if (api->isatty(fd) != 1 ||
            test_kernel->current->descriptors[fd].file->terminal !=
                &test_kernel->console) return 31;
    }
    if (api->isatty(9) != 1 || api->close(9) != 0) return 32;
    api->set_errno(CB_ENOTTY);
    api->yield();
    return api->get_errno() == CB_ENOTTY ? 0 : 33;
}

static int contract_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    struct cb_api_v1 modified;
    struct cb_api_v1 *short_api;
    struct cb_pollfd descriptor = {0, CB_POLLIN, 0};
    struct cb_termios_v1 attributes;
    char data[sizeof(input_bytes)] = {0};
    char *child_argv[] = {(char *)"terminalchild", NULL};
    char flags[5] = "1111";
    char *optional_argv[] = {(char *)"optional", flags, NULL};
    cb_pid_t child;
    int fd, status, index, result;
    size_t sizes[] = {
        offsetof(struct cb_api_v1, poll),
        offsetof(struct cb_api_v1, isatty),
        offsetof(struct cb_api_v1, isatty) + sizeof(api->isatty) - 1,
        offsetof(struct cb_api_v1, tcgetattr),
        offsetof(struct cb_api_v1, tcgetattr) + sizeof(api->tcgetattr) - 1,
        offsetof(struct cb_api_v1, tcsetattr),
        offsetof(struct cb_api_v1, tcsetattr) + sizeof(api->tcsetattr) - 1,
        sizeof(*api)
    };
    const char *size_flags[] = {
        "0000", "0001", "0001", "1001", "1001", "1101", "1101", "1111"
    };
    (void)argc; (void)argv; (void)envp;
    if (test_kernel->console.kernel != test_kernel) return 40;
    fd = api->dup(0);
    if (fd < 0 || api->isatty(fd) != 1 || api->dup2(fd, 9) != 9 ||
        api->close(fd) != 0) return 41;
    if (api->spawn("terminalchild", child_argv, NULL, NULL, 0, &child) != 0)
        return 42;
    api->set_errno(CB_EIO);
    api->yield();
    if (api->get_errno() != CB_EIO || api->isatty(9) != 1 ||
        api->waitpid(child, &status) != child || status != 0 ||
        api->close(9) != 0) return 43;

    for (index = 0; index < (int)(sizeof(sizes) / sizeof(sizes[0])); ++index) {
        /* Actual short allocation, so sanitizer checks any out-of-bounds read. */
        short_api = malloc(sizes[index]);
        if (short_api == NULL) return 44;
        memcpy(short_api, api, sizes[index]);
        short_api->struct_size = (uint32_t)sizes[index];
        memcpy(flags, size_flags[index], sizeof(flags));
        result = cb_libc_start(short_api, 2, optional_argv, terminal_optional_main);
        free(short_api);
        if (result != 0) return result;
    }
    for (index = 0; index < 3; ++index) {
        modified = *api;
        memcpy(flags, "1111", sizeof(flags));
        flags[index] = '0';
        if (index == 0) modified.isatty = NULL;
        if (index == 1) modified.tcgetattr = NULL;
        if (index == 2) modified.tcsetattr = NULL;
        result = cb_libc_start(&modified, 2, optional_argv, terminal_optional_main);
        if (result != 0) return result;
    }
    /* Missing poll is independent of present terminal callbacks, too. */
    modified = *api;
    modified.poll = NULL;
    memcpy(flags, "1110", sizeof(flags));
    result = cb_libc_start(&modified, 2, optional_argv, terminal_optional_main);
    if (result != 0) return result;
    memcpy(flags, "1111", sizeof(flags));
    if (cb_libc_start(api, 2, optional_argv, terminal_optional_main) != 0) return 45;

    /* Rejected attributes do not consume, edit, echo, or translate input. */
    input_reads = input_polls = output_writes = 0;
    if (api->tcgetattr(0, &attributes) != -1 || api->get_errno() != CB_ENOSYS ||
        api->tcsetattr(1, 0, &attributes) != -1 || api->get_errno() != CB_ENOSYS ||
        input_reads != 0 || input_polls != 0 || output_writes != 0) return 46;
    if (api->poll(&descriptor, 1, 0) != 1 || descriptor.revents != CB_POLLIN ||
        input_reads != 0) return 47;
    if (api->read(0, data, sizeof(data)) != (cb_ssize_t)(sizeof(input_bytes)-1) ||
        memcmp(data, input_bytes, sizeof(input_bytes)-1) != 0 ||
        input_reads != 1 || output_writes != 0) return 48;
    fd = api->open("/tmp/redirected-input", CB_O_CREAT | CB_O_RDWR, 0600);
    if (fd < 0 || api->dup2(fd, 0) != 0 || api->close(fd) != 0 ||
        api->isatty(0) != 0 || api->get_errno() != CB_ENOTTY ||
        api->tcgetattr(0, &attributes) != -1 || api->get_errno() != CB_ENOTTY)
        return 49;
    return 0;
}

static const struct cb_program_v1 contract = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "terminalcontract", 0,
    64 * 1024, contract_main
};
static const struct cb_program_v1 inherited = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "terminalchild", 0,
    64 * 1024, inherited_main
};

void cb_test_terminal(void)
{
    struct cb_host_ops_v1 host = *cb_linux_host_ops();
    const char *commands[] = {"libcterminalprobe", "terminalcontract"};
    size_t index;
    int status;
    host.console_poll = input_poll;
    host.console_read = input_read;
    host.console_write = output_write;
    for (index = 0; index < sizeof(commands)/sizeof(commands[0]); ++index) {
        input_position = 0;
        test_kernel = cb_kernel_create(&host);
        if (test_kernel == NULL) abort();
        cb_register_base_programs(test_kernel);
        if (cb_kernel_register(test_kernel, &cb_terminal_probe_program) != 0 ||
            cb_kernel_register(test_kernel, &contract) != 0 ||
            cb_kernel_register(test_kernel, &inherited) != 0 ||
            cb_kernel_boot(test_kernel, commands[index]) != 0) abort();
        status = cb_kernel_run(test_kernel);
        cb_kernel_destroy(test_kernel);
        if (status != 0) {
            fprintf(stderr, "terminal test %s returned %d\n", commands[index], status);
            exit(1);
        }
    }
}
