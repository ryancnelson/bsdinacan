#include "internal.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

struct cb_host_context {
    ucontext_t native;
    void *stack;
    size_t stack_size;
};

/* Keep getcontext's returns-twice attribute out of the allocation routines. */
static int host_capture_context(ucontext_t *context)
{
    return getcontext(context);
}

static void *host_allocate(size_t size)
{
    return calloc(1, size);
}

static void *host_resize(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

static void host_release(void *pointer)
{
    free(pointer);
}

static struct cb_host_context *host_context_root(void)
{
    struct cb_host_context *context = calloc(1, sizeof(*context));
    if (context == NULL || host_capture_context(&context->native) != 0) {
        free(context);
        return NULL;
    }
    return context;
}

static void host_context_trampoline(uintptr_t entry_bits, uintptr_t arg_bits)
{
    void (*entry)(void *) = (void (*)(void *))entry_bits;
    entry((void *)arg_bits);
    abort();
}

static struct cb_host_context *host_context_create(void (*entry)(void *),
                                                    void *arg,
                                                    size_t stack_size)
{
    struct cb_host_context *context = calloc(1, sizeof(*context));
    if (context == NULL)
        return NULL;
    context->stack = malloc(stack_size);
    if (context->stack == NULL) {
        free(context);
        return NULL;
    }
    context->stack_size = stack_size;
    if (host_capture_context(&context->native) != 0) {
        free(context->stack);
        free(context);
        return NULL;
    }
    context->native.uc_stack.ss_sp = context->stack;
    context->native.uc_stack.ss_size = context->stack_size;
    context->native.uc_link = NULL;
    makecontext(&context->native, (void (*)(void))host_context_trampoline, 2,
                (uintptr_t)entry, (uintptr_t)arg);
    return context;
}

static void host_context_switch(struct cb_host_context *from,
                                struct cb_host_context *to)
{
    if (swapcontext(&from->native, &to->native) != 0) {
        perror("swapcontext");
        abort();
    }
}

static void host_context_destroy(struct cb_host_context *context)
{
    if (context == NULL)
        return;
    free(context->stack);
    free(context);
}

static int host_console_poll(int timeout_ms)
{
    struct pollfd descriptor;
    int result;
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN | POLLHUP;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0)
        return result;
    return (descriptor.revents & (POLLIN | POLLHUP)) != 0;
}

static cb_ssize_t host_console_read(void *buffer, size_t count)
{
    ssize_t result;
    do {
        result = read(STDIN_FILENO, buffer, count);
    } while (result < 0 && errno == EINTR);
    return result < 0 ? -CB_EIO : (cb_ssize_t)result;
}

static cb_ssize_t host_console_write(int stream, const void *buffer,
                                     size_t count)
{
    int descriptor = stream == 2 ? STDERR_FILENO : STDOUT_FILENO;
    const unsigned char *cursor = buffer;
    size_t written = 0;
    while (written < count) {
        ssize_t result = write(descriptor, cursor + written, count - written);
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0)
            return -CB_EIO;
        written += (size_t)result;
    }
    return (cb_ssize_t)written;
}

static uint64_t host_monotonic_millis(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static uint64_t host_wall_clock_millis(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0 || now.tv_sec < 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void host_yield(void)
{
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}

static void host_fatal(const char *message)
{
    fprintf(stderr, "cannedBSD: fatal: %s\n", message);
    abort();
}

static const struct cb_host_ops_v1 linux_ops = {
    CB_ABI_VERSION_V1,
    sizeof(linux_ops),
    host_allocate,
    host_resize,
    host_release,
    host_context_root,
    host_context_create,
    host_context_switch,
    host_context_destroy,
    host_console_poll,
    host_console_read,
    host_console_write,
    host_monotonic_millis,
    host_wall_clock_millis,
    host_yield,
    host_fatal
};

const struct cb_host_ops_v1 *cb_linux_host_ops(void)
{
    return &linux_ops;
}
