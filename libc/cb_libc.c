#include "cannedbsd/libc.h"

#include <limits.h>
#include <stdarg.h>

struct cb_libc_file {
    int descriptor;
};

static struct cb_libc_file stdout_file = {1};
static struct cb_libc_file stderr_file = {2};
struct cb_libc_file *const cb_libc_stdout_stream = &stdout_file;
struct cb_libc_file *const cb_libc_stderr_stream = &stderr_file;

static const struct cb_api_v1 *bound_api;

static int api_is_usable(const struct cb_api_v1 *api)
{
    return api != NULL && api->abi_version == CB_ABI_VERSION_V1 &&
           api->struct_size >= offsetof(struct cb_api_v1, poll) && api->read != NULL &&
           api->write != NULL && api->open != NULL && api->close != NULL &&
           api->get_errno != NULL && api->set_errno != NULL &&
           api->strerror != NULL && api->allocate != NULL &&
           api->resize != NULL && api->release != NULL &&
           api->errno_location != NULL && api->environ_location != NULL &&
           api->exit != NULL && api->getopt_state_location != NULL &&
           api->truncate != NULL && api->ftruncate != NULL &&
           api->getprogname != NULL;
}

int cb_libc_start(const struct cb_api_v1 *api, int argc, char *const argv[],
                  cb_libc_main_fn main_function)
{
    if (!api_is_usable(api) || main_function == NULL)
        return 126;
    bound_api = api;
    return main_function(argc, (char **)argv);
}

cb_ssize_t cb_libc_read(int descriptor, void *buffer, size_t count)
{
    return bound_api->read(descriptor, buffer, count);
}

cb_ssize_t cb_libc_write(int descriptor, const void *buffer, size_t count)
{
    return bound_api->write(descriptor, buffer, count);
}

static int translate_open_flags(int flags, int *translated_out)
{
    int translated;
    int known = CB_LIBC_O_ACCMODE | CB_LIBC_O_APPEND | CB_LIBC_O_CREAT |
                CB_LIBC_O_TRUNC;
    if ((flags & ~known) != 0)
        return -1;
    switch (flags & CB_LIBC_O_ACCMODE) {
    case CB_LIBC_O_RDONLY: translated = CB_O_RDONLY; break;
    case CB_LIBC_O_WRONLY: translated = CB_O_WRONLY; break;
    case CB_LIBC_O_RDWR: translated = CB_O_RDWR; break;
    default: return -1;
    }
    if ((flags & CB_LIBC_O_APPEND) != 0)
        translated |= CB_O_APPEND;
    if ((flags & CB_LIBC_O_CREAT) != 0)
        translated |= CB_O_CREAT;
    if ((flags & CB_LIBC_O_TRUNC) != 0)
        translated |= CB_O_TRUNC;
    *translated_out = translated;
    return 0;
}

int cb_libc_open(const char *path, int flags, ...)
{
    uint32_t mode = 0;
    int translated;
    if (translate_open_flags(flags, &translated) < 0) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    if ((flags & CB_LIBC_O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (uint32_t)va_arg(arguments, int);
        va_end(arguments);
    }
    return bound_api->open(path, translated, mode);
}

int cb_libc_close(int descriptor)
{
    return bound_api->close(descriptor);
}

int cb_libc_truncate(const char *path, cb_off_t length)
{
    return bound_api->truncate(path, length);
}

int cb_libc_ftruncate(int descriptor, cb_off_t length)
{
    return bound_api->ftruncate(descriptor, length);
}

void *cb_libc_malloc(size_t size)
{
    return bound_api->allocate(size);
}

void *cb_libc_calloc(size_t count, size_t size)
{
    unsigned char *memory;
    size_t total;
    size_t index;
    if (size != 0 && count > SIZE_MAX / size) {
        bound_api->set_errno(CB_ENOMEM);
        return NULL;
    }
    total = count * size;
    memory = bound_api->allocate(total);
    if (memory == NULL)
        return NULL;
    for (index = 0; index < total; ++index)
        memory[index] = 0;
    return memory;
}

void *cb_libc_realloc(void *pointer, size_t size)
{
    return bound_api->resize(pointer, size);
}

void cb_libc_exit(int status)
{
    bound_api->exit(status);
}

void cb_libc_free(void *pointer)
{
    int saved_error = bound_api->get_errno();
    bound_api->release(pointer);
    bound_api->set_errno(saved_error);
}

int *cb_libc_errno_location(void)
{
    return bound_api->errno_location();
}

char ***cb_libc_environ_location(void)
{
    return bound_api->environ_location();
}

struct cb_getopt_state_v1 *cb_libc_getopt_state_location(void)
{
    return bound_api->getopt_state_location();
}

static void getopt_diagnostic(const char *program, int character)
{
    static const char middle[] = ": illegal option -- ";
    char letter = (char)character;
    bound_api->write(2, program, cb_libc_strlen(program));
    bound_api->write(2, middle, sizeof(middle) - 1);
    bound_api->write(2, &letter, 1);
    bound_api->write(2, "\n", 1);
}

/*
 * Supports flag-only optstrings (any set of single-character flags with no
 * argument), which is everything the commissioned empty-optstring use case
 * (pinned printenv) needs. Deliberately does NOT implement the ':'
 * required-argument convention: that branch had no consumer and no test
 * coverage, so per AGENTS.md's "no speculative surface" rule it does not
 * belong here. A future caller that needs required-argument options is a
 * separate, independently red/green-tested extension, not a silent
 * broadening of this one.
 */
int cb_libc_getopt(int argc, char *const argv[], const char *optstring)
{
    struct cb_getopt_state_v1 *state = bound_api->getopt_state_location();

    if (*state->place == '\0') {
        if (state->optind >= argc || argv[state->optind][0] != '-' ||
            argv[state->optind][1] == '\0')
            return -1;
        if (argv[state->optind][1] == '-' && argv[state->optind][2] == '\0') {
            ++state->optind;
            return -1;
        }
        state->place = argv[state->optind] + 1;
    }

    state->optopt = (int)*state->place++;
    if (*state->place == '\0')
        ++state->optind;
    if (cb_libc_strchr(optstring, state->optopt) == NULL) {
        if (state->opterr)
            getopt_diagnostic(argv[0], state->optopt);
        return (int)'?';
    }
    state->optarg = NULL;
    return state->optopt;
}

char *cb_libc_strerror(int error)
{
    return (char *)bound_api->strerror(error);
}

static int write_all(int descriptor, const char *text, size_t length)
{
    while (length != 0) {
        cb_ssize_t written = bound_api->write(descriptor, text, length);
        if (written <= 0)
            return -1;
        text += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

int cb_libc_puts(const char *text)
{
    size_t length = cb_libc_strlen(text);
    if (write_all(1, text, length) < 0 || write_all(1, "\n", 1) < 0)
        return -1;
    return 0;
}

static int add_output(int descriptor, const char *text, size_t length,
                      int *total)
{
    if (length > (size_t)(INT_MAX - *total)) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    if (write_all(descriptor, text, length) < 0)
        return -1;
    *total += (int)length;
    return 0;
}

static int format_output(int descriptor, const char *format,
                         va_list arguments)
{
    const char *cursor = format;
    int total = 0;

    if (format == NULL) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    while (*cursor != '\0') {
        const char *literal = cursor;
        while (*cursor != '\0' && *cursor != '%')
            ++cursor;
        if (add_output(descriptor, literal, (size_t)(cursor - literal),
                       &total) < 0)
            return -1;
        if (*cursor == '\0')
            break;
        ++cursor;
        if (*cursor == '%') {
            if (add_output(descriptor, "%", 1, &total) < 0)
                return -1;
            ++cursor;
        } else if (*cursor == 's') {
            const char *text = va_arg(arguments, const char *);
            if (text == NULL)
                text = "(null)";
            if (add_output(descriptor, text, cb_libc_strlen(text), &total) < 0)
                return -1;
            ++cursor;
        } else {
            bound_api->set_errno(CB_EINVAL);
            return -1;
        }
    }
    return total;
}

int cb_libc_printf(const char *format, ...)
{
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = format_output(1, format, arguments);
    va_end(arguments);
    return result;
}

int cb_libc_fprintf(struct cb_libc_file *stream, const char *format, ...)
{
    va_list arguments;
    int result;
    if (stream != cb_libc_stdout_stream && stream != cb_libc_stderr_stream) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    va_start(arguments, format);
    result = format_output(stream->descriptor, format, arguments);
    va_end(arguments);
    return result;
}

void cb_libc_errx(int eval, const char *fmt, ...)
{
    va_list arguments;
    const char *name = bound_api->getprogname();
    if (name == NULL)
        name = "";
    write_all(2, name, cb_libc_strlen(name));
    write_all(2, ": ", 2);
    va_start(arguments, fmt);
    format_output(2, fmt, arguments);
    va_end(arguments);
    write_all(2, "\n", 1);
    cb_libc_exit(eval);
}

void cb_libc_err(int eval, const char *fmt, ...)
{
    int saved_error = bound_api->get_errno();
    const char *name = bound_api->getprogname();
    const char *error_text = bound_api->strerror(saved_error);
    va_list arguments;
    if (name == NULL)
        name = "";
    write_all(2, name, cb_libc_strlen(name));
    write_all(2, ": ", 2);
    if (fmt != NULL) {
        va_start(arguments, fmt);
        format_output(2, fmt, arguments);
        va_end(arguments);
        write_all(2, ": ", 2);
    }
    write_all(2, error_text, cb_libc_strlen(error_text));
    write_all(2, "\n", 1);
    cb_libc_exit(eval);
}

int cb_libc_pipe(int fds[2])
{
    return bound_api->pipe(fds);
}

int cb_libc_poll(struct cb_pollfd *fds, size_t nfds, int timeout)
{
    if (bound_api->struct_size < sizeof(*bound_api) || bound_api->poll == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->poll(fds, nfds, timeout);
}
