#include "cannedbsd/libc.h"

#include <stdarg.h>

static const struct cb_api_v1 *bound_api;

static int api_is_usable(const struct cb_api_v1 *api)
{
    return api != NULL && api->abi_version == CB_ABI_VERSION_V1 &&
           api->struct_size >= sizeof(*api) && api->read != NULL &&
           api->write != NULL && api->open != NULL && api->close != NULL &&
           api->get_errno != NULL && api->set_errno != NULL &&
           api->strerror != NULL && api->allocate != NULL &&
           api->resize != NULL && api->release != NULL &&
           api->errno_location != NULL;
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
