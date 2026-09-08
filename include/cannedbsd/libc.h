#ifndef CANNEDBSD_LIBC_H
#define CANNEDBSD_LIBC_H

#include "cannedbsd/abi.h"

#include <stddef.h>
#include <stdint.h>

enum cb_libc_open_flag {
    CB_LIBC_O_RDONLY = 0x0000,
    CB_LIBC_O_WRONLY = 0x0001,
    CB_LIBC_O_RDWR = 0x0002,
    CB_LIBC_O_ACCMODE = 0x0003,
    CB_LIBC_O_APPEND = 0x0008,
    CB_LIBC_O_CREAT = 0x0200,
    CB_LIBC_O_TRUNC = 0x0400
};

enum cb_libc_locale_category { CB_LIBC_LC_ALL = 0 };
char *cb_libc_setlocale(int category, const char *locale);

typedef int (*cb_libc_main_fn)(int argc, char *argv[]);
struct cb_libc_file;

int cb_libc_start(const struct cb_api_v1 *api, int argc, char *const argv[],
                  cb_libc_main_fn main_function);
cb_ssize_t cb_libc_read(int descriptor, void *buffer, size_t count);
cb_ssize_t cb_libc_write(int descriptor, const void *buffer, size_t count);
int cb_libc_open(const char *path, int flags, ...);
int cb_libc_close(int descriptor);
int cb_libc_truncate(const char *path, cb_off_t length);
int cb_libc_ftruncate(int descriptor, cb_off_t length);
int cb_libc_isatty(int descriptor);
int cb_libc_tcgetattr(int descriptor, struct cb_termios_v1 *attributes);
int cb_libc_tcsetattr(int descriptor, int action,
                      const struct cb_termios_v1 *attributes);
int cb_libc_pipe(int fds[2]);
int cb_libc_poll(struct cb_pollfd *fds, size_t nfds, int timeout);
void *cb_libc_malloc(size_t size);
void *cb_libc_calloc(size_t count, size_t size);
void *cb_libc_realloc(void *pointer, size_t size);
void cb_libc_free(void *pointer);
int *cb_libc_errno_location(void);
char ***cb_libc_environ_location(void);
struct cb_getopt_state_v1 *cb_libc_getopt_state_location(void);
int cb_libc_getopt(int argc, char *const argv[], const char *optstring);
char *cb_libc_strerror(int error);
int cb_libc_puts(const char *text);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(struct cb_libc_file *stream, const char *format, ...);
extern struct cb_libc_file *const cb_libc_stdout_stream;
extern struct cb_libc_file *const cb_libc_stderr_stream;
size_t cb_libc_strlen(const char *text);
int cb_libc_strcmp(const char *left, const char *right);
void *cb_libc_memcpy(void *destination, const void *source, size_t count);
void *cb_libc_memmove(void *destination, const void *source, size_t count);
int cb_libc_memcmp(const void *left, const void *right, size_t count);
char *cb_libc_strchr(const char *text, int character);

#define CB_LIBC_PROGRAM(symbol, command_name, main_function) \
    static int symbol##_start(const struct cb_api_v1 *api, int argc, \
                              char *const argv[], char *const envp[]) \
    { \
        (void)envp; \
        return cb_libc_start(api, argc, argv, main_function); \
    } \
    const struct cb_program_v1 symbol = { \
        CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), command_name, 0, \
        64 * 1024, symbol##_start \
    }

#endif
