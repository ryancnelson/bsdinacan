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

typedef int (*cb_libc_main_fn)(int argc, char *argv[]);

int cb_libc_start(const struct cb_api_v1 *api, int argc, char *const argv[],
                  cb_libc_main_fn main_function);
cb_ssize_t cb_libc_read(int descriptor, void *buffer, size_t count);
cb_ssize_t cb_libc_write(int descriptor, const void *buffer, size_t count);
int cb_libc_open(const char *path, int flags, ...);
int cb_libc_close(int descriptor);
void *cb_libc_malloc(size_t size);
void cb_libc_free(void *pointer);
int *cb_libc_errno_location(void);
char *cb_libc_strerror(int error);

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
