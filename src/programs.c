#include "internal.h"

#include <stdio.h>
#include <string.h>

extern const struct cb_program_v1 cb_wc_program;

static int write_all(const struct cb_api_v1 *api, int descriptor,
                     const void *buffer, size_t count)
{
    const unsigned char *cursor = buffer;
    while (count > 0) {
        cb_ssize_t written = api->write(descriptor, cursor, count);
        if (written <= 0)
            return -1;
        cursor += (size_t)written;
        count -= (size_t)written;
    }
    return 0;
}

static void report_error(const struct cb_api_v1 *api, const char *command,
                         const char *subject)
{
    char message[512];
    int count = snprintf(message, sizeof(message), "%s: %s: %s\n", command,
                         subject, api->strerror(api->get_errno()));
    if (count > 0)
        write_all(api, 2, message, (size_t)count);
}

static int echo_main(const struct cb_api_v1 *api, int argc,
                     char *const argv[], char *const envp[])
{
    int index = 1;
    int newline = 1;
    (void)envp;
    if (index < argc && strcmp(argv[index], "-n") == 0) {
        newline = 0;
        ++index;
    }
    while (index < argc) {
        if (index > (newline ? 1 : 2) && write_all(api, 1, " ", 1) < 0)
            return 1;
        if (write_all(api, 1, argv[index], strlen(argv[index])) < 0)
            return 1;
        ++index;
    }
    if (newline && write_all(api, 1, "\n", 1) < 0)
        return 1;
    return 0;
}

static int cat_descriptor(const struct cb_api_v1 *api, int descriptor)
{
    unsigned char buffer[1024];
    for (;;) {
        cb_ssize_t count = api->read(descriptor, buffer, sizeof(buffer));
        if (count < 0)
            return -1;
        if (count == 0)
            return 0;
        if (write_all(api, 1, buffer, (size_t)count) < 0)
            return -1;
    }
}

static int cat_main(const struct cb_api_v1 *api, int argc,
                    char *const argv[], char *const envp[])
{
    int status = 0;
    int index;
    (void)envp;
    if (argc == 1) {
        if (cat_descriptor(api, 0) < 0) {
            report_error(api, "cat", "standard input");
            return 1;
        }
        return 0;
    }
    for (index = 1; index < argc; ++index) {
        int descriptor;
        if (strcmp(argv[index], "-") == 0) {
            if (cat_descriptor(api, 0) < 0)
                status = 1;
            continue;
        }
        descriptor = api->open(argv[index], CB_O_RDONLY, 0);
        if (descriptor < 0) {
            report_error(api, "cat", argv[index]);
            status = 1;
            continue;
        }
        if (cat_descriptor(api, descriptor) < 0) {
            report_error(api, "cat", argv[index]);
            status = 1;
        }
        api->close(descriptor);
    }
    return status;
}

static int expand_set(const char *set, unsigned char values[256],
                      size_t *length_out)
{
    size_t used = 0;
    size_t index;
    for (index = 0; set[index] != '\0'; ++index) {
        unsigned char first = (unsigned char)set[index];
        if (set[index + 1] == '-' && set[index + 2] != '\0') {
            unsigned char last = (unsigned char)set[index + 2];
            unsigned value;
            if (first > last)
                return -1;
            for (value = first; value <= last; ++value) {
                if (used == 256)
                    return -1;
                values[used++] = (unsigned char)value;
            }
            index += 2;
        } else {
            if (used == 256)
                return -1;
            values[used++] = first;
        }
    }
    *length_out = used;
    return 0;
}

static int tr_main(const struct cb_api_v1 *api, int argc,
                   char *const argv[], char *const envp[])
{
    unsigned char from[256];
    unsigned char to[256];
    unsigned char map[256];
    unsigned char buffer[1024];
    size_t from_length;
    size_t to_length;
    size_t index;
    (void)envp;
    if (argc != 3 || expand_set(argv[1], from, &from_length) < 0 ||
        expand_set(argv[2], to, &to_length) < 0 || to_length == 0) {
        static const char usage[] = "usage: tr string1 string2\n";
        write_all(api, 2, usage, sizeof(usage) - 1);
        return 2;
    }
    for (index = 0; index < sizeof(map); ++index)
        map[index] = (unsigned char)index;
    for (index = 0; index < from_length; ++index)
        map[from[index]] = to[index < to_length ? index : to_length - 1];
    for (;;) {
        cb_ssize_t count = api->read(0, buffer, sizeof(buffer));
        size_t byte;
        if (count < 0) {
            report_error(api, "tr", "standard input");
            return 1;
        }
        if (count == 0)
            return 0;
        for (byte = 0; byte < (size_t)count; ++byte)
            buffer[byte] = map[buffer[byte]];
        if (write_all(api, 1, buffer, (size_t)count) < 0) {
            report_error(api, "tr", "standard output");
            return 1;
        }
    }
}

static int true_main(const struct cb_api_v1 *api, int argc,
                     char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    return 0;
}

static int false_main(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    (void)api;
    (void)argc;
    (void)argv;
    (void)envp;
    return 1;
}

#define PROGRAM_DESCRIPTOR(symbol, command_name, entry) \
    static const struct cb_program_v1 symbol = { \
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), command_name, 0, \
        64 * 1024, entry \
    }

PROGRAM_DESCRIPTOR(echo_program, "echo", echo_main);
PROGRAM_DESCRIPTOR(cat_program, "cat", cat_main);
PROGRAM_DESCRIPTOR(tr_program, "tr", tr_main);
PROGRAM_DESCRIPTOR(true_program, "true", true_main);
PROGRAM_DESCRIPTOR(false_program, "false", false_main);

void cb_register_base_programs(struct cb_kernel *kernel)
{
    const struct cb_program_v1 *programs[] = {
        &cb_shell_program,
        &cb_shell_builtin_program,
        &echo_program,
        &cat_program,
        &tr_program,
        &true_program,
        &false_program,
        &cb_wc_program
    };
    size_t index;
    for (index = 0; index < sizeof(programs) / sizeof(programs[0]); ++index) {
        if (cb_kernel_register(kernel, programs[index]) < 0)
            kernel->host->fatal("failed to register base program");
    }
}
