#include "cannedbsd/libc.h"

#ifndef EOF
#define EOF (-1)
#endif

#include <limits.h>
#include <stdarg.h>

struct cb_libc_file {
    int descriptor;
    int eof;
    int error;
    struct cb_libc_file *next;
};

static struct cb_libc_file stdin_file = {0, 0, 0, NULL};
static struct cb_libc_file stdout_file = {1, 0, 0, NULL};
static struct cb_libc_file stderr_file = {2, 0, 0, NULL};
struct cb_libc_file *const cb_libc_stdin_stream = &stdin_file;
struct cb_libc_file *const cb_libc_stdout_stream = &stdout_file;
struct cb_libc_file *const cb_libc_stderr_stream = &stderr_file;

static const struct cb_api_v1 *bound_api;

static int api_is_usable(const struct cb_api_v1 *api)
{
    /* Keep the established mandatory prefix through getprogname. Poll,
       terminal, dirname and directory operations remain optional tails;
       wrappers check their own field ends and callbacks when called. */
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

/* Each of opendir/readdir/closedir checks only its own field here, not
   the other two -- matching cb_vfs_node_ops's per-operation independence
   (truncate and child_at are each checked on their own): a table missing
   only readdir must still let opendir()/closedir() work, since readdir's
   absence alone never prevents acquiring or releasing anything. A table
   missing only closedir is NOT an equally safe case, despite otherwise
   following the same per-field shape: closedir is the only thing that
   can ever release what opendir() acquires, so cb_libc_opendir (below)
   additionally requires closedir_api_available() itself before it ever
   calls bound_api->opendir() -- it does not rely on this helper's
   independence alone. See cb_libc_opendir's own comment for why. */
static int opendir_api_available(void)
{
    return bound_api->struct_size >=
               offsetof(struct cb_api_v1, opendir) +
                   sizeof(bound_api->opendir) &&
           bound_api->opendir != NULL;
}

static int readdir_api_available(void)
{
    return bound_api->struct_size >=
               offsetof(struct cb_api_v1, readdir) +
                   sizeof(bound_api->readdir) &&
           bound_api->readdir != NULL;
}

static int closedir_api_available(void)
{
    return bound_api->struct_size >=
               offsetof(struct cb_api_v1, closedir) +
                   sizeof(bound_api->closedir) &&
           bound_api->closedir != NULL;
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

static void getopt_diagnostic(const char *program, int character,
                              const char *message)
{
    char letter = (char)character;
    bound_api->write(2, program, cb_libc_strlen(program));
    bound_api->write(2, message, cb_libc_strlen(message));
    bound_api->write(2, &letter, 1);
    bound_api->write(2, "\n", 1);
}

/* Flags and single-colon required arguments, with no operand permutation or
   optional-argument extension. The existing task state owns the scan cursor. */
int cb_libc_getopt(int argc, char *const argv[], const char *optstring)
{
    struct cb_getopt_state_v1 *state = bound_api->getopt_state_location();
    const char *option;
    state->optarg = NULL;
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
    option = cb_libc_strchr(optstring, state->optopt);
    if (state->optopt == ':' || option == NULL) {
        if (state->opterr && optstring[0] != ':')
            getopt_diagnostic(argv[0], state->optopt, ": illegal option -- ");
        return (int)'?';
    }
    if (option[1] == ':') {
        if (*state->place != '\0') {
            state->optarg = state->place;
            ++state->optind;
        } else if (state->optind < argc) {
            /* Even '-' and '--' are values when an argument is required. */
            state->optarg = argv[state->optind++];
        } else {
            if (state->opterr && optstring[0] != ':')
                getopt_diagnostic(argv[0], state->optopt,
                                  ": option requires an argument -- ");
            return optstring[0] == ':' ? (int)':' : (int)'?';
        }
        state->place = (char *)"";
    }
    return state->optopt;
}

const char *cb_libc_getprogname(void)
{
    const char *name = bound_api->getprogname();
    const char *component = name;
    if (name == NULL)
        return NULL;
    for (; *name != '\0'; ++name) {
        if (*name == '/')
            component = name + 1;
    }
    return component;
}

void cb_libc_setprogname(const char *name)
{
    /* Startup established the identity before main, as in NetBSD crt0.
       A later portable main's setprogname call cannot rename that task. */
    (void)name;
}

char *cb_libc_strerror(int error)
{
    return (char *)bound_api->strerror(error);
}

static struct cb_input_state_v1 *input_state(void)
{
    struct cb_input_state_v1 *state = NULL;
    int saved_errno = bound_api->get_errno();
    if (bound_api->abi_version == CB_ABI_VERSION_V1 &&
        bound_api->struct_size >= offsetof(struct cb_api_v1, input_state_location) +
                                  sizeof(bound_api->input_state_location) &&
        bound_api->input_state_location != NULL)
        state = bound_api->input_state_location();
    bound_api->set_errno(saved_errno);
    if (state == NULL || state->abi_version != CB_ABI_VERSION_V1 ||
        state->struct_size < CB_INPUT_STATE_V1_MIN_SIZE)
        return NULL;
    return state;
}

/* Stage 1 remains sufficient for stdin reads/status. Ownership operations and
 * dynamic pointers need the independently guarded stage 2 tail. */
static int input_streams_available(const struct cb_input_state_v1 *state)
{
    return state->struct_size >= CB_INPUT_STREAMS_V1_MIN_SIZE;
}

static struct cb_libc_file *find_input_stream(struct cb_input_state_v1 *state,
                                             struct cb_libc_file *stream,
                                             struct cb_libc_file **previous)
{
    struct cb_libc_file *node = state->input_streams;
    *previous = NULL;
    while (node != NULL) {
        if (node == stream)
            return node;
        *previous = node;
        node = node->next;
    }
    return NULL;
}

struct input_reference {
    int descriptor;
    int *eof;
    int *error;
};

static int resolve_input(struct cb_libc_file *stream, struct input_reference *ref)
{
    struct cb_input_state_v1 *state;
    struct cb_libc_file *node, *previous;
    if (stream == NULL || stream == cb_libc_stdout_stream ||
        stream == cb_libc_stderr_stream) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    state = input_state();
    if (state == NULL || (stream != cb_libc_stdin_stream &&
                         !input_streams_available(state))) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    if (stream == cb_libc_stdin_stream) {
        if (input_streams_available(state) && state->stdin_closed) {
            bound_api->set_errno(CB_EINVAL);
            return -1;
        }
        ref->descriptor = 0;
        ref->eof = &state->stdin_eof;
        ref->error = &state->stdin_error;
        return 0;
    }
    /* Only traverse owned nodes. Never dereference the supplied identity. */
    node = find_input_stream(state, stream, &previous);
    if (node == NULL) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    ref->descriptor = node->descriptor;
    ref->eof = &node->eof;
    ref->error = &node->error;
    return 0;
}

struct cb_libc_file *cb_libc_fopen(const char *path, const char *mode)
{
    struct cb_input_state_v1 *state;
    struct cb_libc_file *stream;
    int descriptor, saved_errno = bound_api->get_errno();
    if (path == NULL || mode == NULL ||
        (cb_libc_strcmp(mode, "r") != 0 && cb_libc_strcmp(mode, "rb") != 0)) {
        bound_api->set_errno(CB_EINVAL);
        return NULL;
    }
    state = input_state();
    if (state == NULL || !input_streams_available(state)) {
        bound_api->set_errno(CB_ENOSYS);
        return NULL;
    }
    /* Startup's mandatory prefix guarantees open/close/allocate/release. */
    descriptor = bound_api->open(path, CB_O_RDONLY, 0);
    if (descriptor < 0)
        return NULL;
    stream = bound_api->allocate(sizeof(*stream));
    if (stream == NULL) {
        bound_api->close(descriptor);
        bound_api->set_errno(CB_ENOMEM);
        return NULL;
    }
    stream->descriptor = descriptor;
    stream->eof = stream->error = 0;
    stream->next = state->input_streams;
    state->input_streams = stream;
    bound_api->set_errno(saved_errno);
    return stream;
}

int cb_libc_fclose(struct cb_libc_file *stream)
{
    struct cb_input_state_v1 *state;
    struct cb_libc_file *node, *previous;
    int result, error, saved_errno = bound_api->get_errno();
    if (stream == NULL || stream == cb_libc_stdout_stream ||
        stream == cb_libc_stderr_stream) {
        bound_api->set_errno(CB_EINVAL);
        return EOF;
    }
    state = input_state();
    if (state == NULL || !input_streams_available(state)) {
        bound_api->set_errno(CB_ENOSYS);
        return EOF;
    }
    if (stream == cb_libc_stdin_stream) {
        if (state->stdin_closed) {
            bound_api->set_errno(CB_EINVAL);
            return EOF;
        }
        state->stdin_closed = 1;
        result = bound_api->close(0);
        error = bound_api->get_errno();
    } else {
        node = find_input_stream(state, stream, &previous);
        if (node == NULL) {
            bound_api->set_errno(CB_EINVAL);
            return EOF;
        }
        if (previous == NULL)
            state->input_streams = node->next;
        else
            previous->next = node->next;
        result = bound_api->close(node->descriptor);
        error = bound_api->get_errno();
        bound_api->release(node);
    }
    bound_api->set_errno(result < 0 ? error : saved_errno);
    return result < 0 ? EOF : 0;
}

/* The caller has resolved a live stream and supplied a positive bounded count. */
static cb_ssize_t read_input(struct input_reference *ref, void *buffer, size_t count)
{
    cb_ssize_t result;
    int saved_errno = bound_api->get_errno();
    if (*ref->eof)
        return 0;
    result = bound_api->read(ref->descriptor, buffer, count);
    if (result < 0) {
        *ref->error = 1;
        return result;
    }
    /* Compare before narrowing a 64-bit callback result on the 32-bit target. */
    if ((uint64_t)result > (uint64_t)count) {
        *ref->error = 1;
        bound_api->set_errno(CB_EIO);
        return -1;
    }
    if (result == 0)
        *ref->eof = 1;
    bound_api->set_errno(saved_errno);
    return result;
}

int cb_libc_getc(struct cb_libc_file *stream)
{
    struct input_reference ref;
    unsigned char byte;
    if (resolve_input(stream, &ref) < 0)
        return EOF;
    return read_input(&ref, &byte, 1) <= 0 ? EOF : (int)byte;
}

size_t cb_libc_fread(void *buffer, size_t size, size_t count,
                     struct cb_libc_file *stream)
{
    struct input_reference ref;
    size_t total, done = 0;
    if (size == 0 || count == 0)
        return 0;
    if (resolve_input(stream, &ref) < 0)
        return 0;
    if (size > SIZE_MAX / count) {
        bound_api->set_errno(CB_EOVERFLOW);
        return 0;
    }
    if (buffer == NULL) {
        bound_api->set_errno(CB_EINVAL);
        return 0;
    }
    total = size * count;
    while (done < total) {
        size_t request = total - done;
        cb_ssize_t result;
        if ((uint64_t)request > (uint64_t)INT64_MAX)
            request = (size_t)INT64_MAX;
        result = read_input(&ref, (unsigned char *)buffer + done, request);
        if (result <= 0)
            break;
        done += (size_t)result;
    }
    return done / size;
}

int cb_libc_feof(struct cb_libc_file *stream)
{
    struct input_reference ref;
    if (stream == cb_libc_stdout_stream || stream == cb_libc_stderr_stream)
        return 0;
    if (resolve_input(stream, &ref) < 0)
        return 0;
    return *ref.eof;
}

static struct cb_stdio_state_v1 *stdio_state(void)
{
    struct cb_stdio_state_v1 *state = NULL;
    int saved_errno = bound_api->get_errno();
    if (bound_api->abi_version == CB_ABI_VERSION_V1 &&
        bound_api->struct_size >= offsetof(struct cb_api_v1, stdio_state_location) +
                                  sizeof(bound_api->stdio_state_location) &&
        bound_api->stdio_state_location != NULL)
        state = bound_api->stdio_state_location();
    bound_api->set_errno(saved_errno);
    if (state == NULL || state->abi_version != CB_ABI_VERSION_V1 ||
        state->struct_size < sizeof(*state))
        return NULL;
    return state;
}

static void mark_stdio_error(int descriptor)
{
    struct cb_stdio_state_v1 *state = stdio_state();
    if (state != NULL) {
        if (descriptor == 1)
            state->stdout_error = 1;
        else if (descriptor == 2)
            state->stderr_error = 1;
    }
}

static int write_all(int descriptor, const char *text, size_t length)
{
    int saved_incoming_errno = bound_api->get_errno();
    while (length != 0) {
        cb_ssize_t written = bound_api->write(descriptor, text, length);
        if (written < 0) {
            int actual_error = bound_api->get_errno();
            mark_stdio_error(descriptor);
            bound_api->set_errno(actual_error);
            return -1;
        }
        if (written == 0) {
            mark_stdio_error(descriptor);
            bound_api->set_errno(CB_EIO);
            return -1;
        }
        text += (size_t)written;
        length -= (size_t)written;
    }
    bound_api->set_errno(saved_incoming_errno);
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


void cb_libc_warn(const char *fmt, ...)
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
    bound_api->set_errno(saved_error);
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
    if (bound_api->struct_size < offsetof(struct cb_api_v1, poll) +
                                 sizeof(bound_api->poll) || bound_api->poll == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->poll(fds, nfds, timeout);
}

int cb_libc_isatty(int descriptor)
{
    if (bound_api->struct_size < offsetof(struct cb_api_v1, isatty) +
                                 sizeof(bound_api->isatty) ||
        bound_api->isatty == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return 0;
    }
    return bound_api->isatty(descriptor);
}

int cb_libc_tcgetattr(int descriptor, struct cb_termios_v1 *attributes)
{
    if (bound_api->struct_size < offsetof(struct cb_api_v1, tcgetattr) +
                                 sizeof(bound_api->tcgetattr) ||
        bound_api->tcgetattr == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->tcgetattr(descriptor, attributes);
}

int cb_libc_tcsetattr(int descriptor, int action,
                      const struct cb_termios_v1 *attributes)
{
    if (bound_api->struct_size < offsetof(struct cb_api_v1, tcsetattr) +
                                 sizeof(bound_api->tcsetattr) ||
        bound_api->tcsetattr == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->tcsetattr(descriptor, action, attributes);
}

static int locale_is_c(const char *name)
{
    return cb_libc_strcmp(name, "C") == 0 || cb_libc_strcmp(name, "POSIX") == 0;
}

char *cb_libc_setlocale(int category, const char *locale)
{
    static const char *const category_variables[] = {
        "LC_COLLATE", "LC_CTYPE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME",
        "LC_MESSAGES"
    };
    const char *selected;
    size_t index;
    if (category != CB_LIBC_LC_ALL)
        return NULL;
    /* Every successful operation denotes the same immutable C profile. The
     * borrowed result is not caller-writable, as for ordinary setlocale. */
    if (locale == NULL)
        return (char *)"C";
    if (*locale != '\0')
        return locale_is_c(locale) ? (char *)"C" : NULL;
    /* getenv is in the existing mandatory-size prefix, but its callback was
     * never mandatory for libc startup. Do not fall back to the host. */
    if (bound_api == NULL || bound_api->getenv == NULL)
        return NULL;
    selected = bound_api->getenv("LC_ALL");
    if (selected != NULL && *selected != '\0')
        return locale_is_c(selected) ? (char *)"C" : NULL;
    for (index = 0; index < sizeof(category_variables) /
                              sizeof(category_variables[0]); ++index) {
        selected = bound_api->getenv(category_variables[index]);
        if (selected == NULL || *selected == '\0')
            selected = bound_api->getenv("LANG");
        if (selected != NULL && *selected != '\0' && !locale_is_c(selected))
            return NULL;
    }
    return (char *)"C";
}

char *cb_libc_dirname(char *path)
{
    char *upstream;
    char *owned;
    size_t length;
    if (bound_api->struct_size <
            offsetof(struct cb_api_v1, dirname_buffer_location) +
                sizeof(bound_api->dirname_buffer_location) ||
        bound_api->dirname_buffer_location == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return NULL;
    }
    upstream = cb_libc_dirname_upstream(path);
    owned = bound_api->dirname_buffer_location();
    /* Copy out of the upstream static into this task's own buffer before
       returning: that static is shared process-wide across every
       cooperatively scheduled task, so a task that does not immediately
       consume the result (e.g. yields before reading through the
       pointer) could otherwise have it silently overwritten by another
       task's unrelated dirname() call. Upstream's own result is already
       bounded to its PATH_MAX (== CB_PATH_MAX here), so this never
       truncates anything upstream itself would not have. */
    length = cb_libc_strlen(upstream);
    cb_libc_memcpy(owned, upstream, length + 1);
    return owned;
}

char *cb_libc_basename(char *path)
{
    char *upstream;
    char *owned;
    size_t length;
    if (bound_api->struct_size <
            offsetof(struct cb_api_v1, basename_buffer_location) +
                sizeof(bound_api->basename_buffer_location) ||
        bound_api->basename_buffer_location == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return NULL;
    }
    upstream = cb_libc_basename_upstream(path);
    owned = bound_api->basename_buffer_location();
    /* Copy out of the upstream static into this task's own buffer before
       returning, exactly like cb_libc_dirname -- and into a SEPARATE
       buffer/accessor from dirname's: reusing dirname's buffer would let
       a basename() call silently invalidate an already-returned
       dirname() result in the same task, coupling two otherwise
       independent pinned imports for no reason. Upstream's own result is
       already bounded to its PATH_MAX (== CB_PATH_MAX here), so this
       never truncates anything upstream itself would not have. */
    length = cb_libc_strlen(upstream);
    cb_libc_memcpy(owned, upstream, length + 1);
    return owned;
}

struct cb_libc_dir {
    int descriptor;       /* the opaque runtime handle; meaningless outside
                              the runtime that issued it */
    struct dirent entry;  /* reused every readdir() call, like FILE* */
};

struct cb_libc_dir *cb_libc_opendir(const char *path)
{
    struct cb_libc_dir *dir;
    int descriptor;
    /* closedir must be usable too, not just opendir: opendir is the only
       thing that acquires the raw runtime descriptor, and closedir is the
       only thing that can ever release it (readdir cannot). A table
       missing closedir would otherwise let this acquire a descriptor that
       nothing -- not even a later allocation failure's own cleanup below
       -- can ever release, leaking it for the rest of the task's
       lifetime. Requiring closedir up front means that leak path simply
       cannot be reached. readdir is not required here: its absence alone
       never prevents cleanup, so opendir()/closedir() must still work
       without it. */
    if (!opendir_api_available() || !closedir_api_available()) {
        bound_api->set_errno(CB_ENOSYS);
        return NULL;
    }
    descriptor = bound_api->opendir(path);
    if (descriptor < 0)
        return NULL;
    dir = bound_api->allocate(sizeof(*dir));
    if (dir == NULL) {
        bound_api->closedir(descriptor);
        bound_api->set_errno(CB_ENOMEM);
        return NULL;
    }
    dir->descriptor = descriptor;
    return dir;
}

struct dirent *cb_libc_readdir(struct cb_libc_dir *dirp)
{
    uint64_t inode;
    uint32_t type;
    if (dirp == NULL) {
        bound_api->set_errno(CB_EBADF);
        return NULL;
    }
    if (!readdir_api_available()) {
        bound_api->set_errno(CB_ENOSYS);
        return NULL;
    }
    if (bound_api->readdir(dirp->descriptor, dirp->entry.d_name,
                           sizeof(dirp->entry.d_name), &inode, &type) < 0)
        return NULL;
    if (dirp->entry.d_name[0] == '\0')
        return NULL;
    dirp->entry.d_ino = inode;
    switch (type) {
    case CB_NODE_REGULAR: dirp->entry.d_type = DT_REG; break;
    case CB_NODE_DIRECTORY: dirp->entry.d_type = DT_DIR; break;
    default: dirp->entry.d_type = DT_UNKNOWN; break;
    }
    return &dirp->entry;
}

int cb_libc_closedir(struct cb_libc_dir *dirp)
{
    int result;
    if (dirp == NULL) {
        bound_api->set_errno(CB_EBADF);
        return -1;
    }
    if (!closedir_api_available()) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    result = bound_api->closedir(dirp->descriptor);
    bound_api->release(dirp);
    return result;
}

int cb_libc_putchar(int character)
{
    unsigned char byte = (unsigned char)character;
    if (stdio_state() == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return EOF;
    }
    if (write_all(1, (const char *)&byte, 1) < 0)
        return EOF;
    return byte;
}

int cb_libc_fflush(struct cb_libc_file *stream)
{
    if (stream != NULL && stream != cb_libc_stdout_stream &&
        stream != cb_libc_stderr_stream) {
        bound_api->set_errno(CB_EINVAL);
        return EOF;
    }
    if (stdio_state() == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return EOF;
    }
    return 0;
}

int cb_libc_ferror(struct cb_libc_file *stream)
{
    struct cb_stdio_state_v1 *state;
    if (stream != cb_libc_stdout_stream && stream != cb_libc_stderr_stream) {
        struct input_reference ref;
        if (resolve_input(stream, &ref) < 0)
            return 1;
        return *ref.error;
    }
    state = stdio_state();
    if (state == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return 1;
    }
    return stream == cb_libc_stdout_stream ? state->stdout_error : state->stderr_error;
}
