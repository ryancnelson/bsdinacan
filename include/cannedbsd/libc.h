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
struct cb_libc_dir;

/*
 * Defined here, not in the ordinary-facing libc/include/dirent.h, so both
 * cb_libc.c (which must write d_ino/d_type/d_name) and dirent.h (which
 * exposes them to ordinary source by field access, unlike the opaque
 * FILE/DIR handles) share exactly one definition. Unlike cb_vfs_node,
 * this struct carries no runtime-private pointers -- it is plain data -- so
 * defining it at this layer does not cross the veneer's layer boundary.
 * d_ino is spelled uint64_t rather than ino_t (defined only in the
 * ordinary-facing libc/include/sys/types.h) because the two are the same
 * type either way and this header cannot reach that ordinary include path.
 */
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_DIR 2

struct dirent {
    uint64_t d_ino;
    unsigned char d_type;
    char d_name[CB_PATH_MAX];
};

int cb_libc_start(const struct cb_api_v1 *api, int argc, char *const argv[],
                  cb_libc_main_fn main_function);
cb_ssize_t cb_libc_read(int descriptor, void *buffer, size_t count);
cb_ssize_t cb_libc_write(int descriptor, const void *buffer, size_t count);
int cb_libc_open(const char *path, int flags, ...);
int cb_libc_close(int descriptor);
int cb_libc_truncate(const char *path, cb_off_t length);
int cb_libc_ftruncate(int descriptor, cb_off_t length);
/* Thin pass-through to bound_api->stat, in the same spirit as every other
   cb_libc_* wrapper in this header. stat/fstat sit before the api_is_usable
   struct_size checkpoint (offsetof(..., poll)) but are not in its NULL-check
   list, so unlike truncate/ftruncate this one still guards against a bound
   table that leaves stat unset. Exists so libc/cb_fts.c (a separate
   translation unit with no access to cb_libc.c's private bound_api) can
   populate an FTSENT's fts_statp without any new ABI surface. */
int cb_libc_stat(const char *path, struct cb_stat_v1 *stat_buffer);
/* Thin pass-throughs to bound_api->unlink/rmdir. unlink has been on the
   base table since before this project's optional-extension convention
   existed, so only a NULL check is needed (matching cb_libc_stat above).
   rmdir was appended by VFS-04, so it additionally needs the struct_size
   guard every appended field gets (matching cb_libc_opendir's family). */
int cb_libc_unlink(const char *path);
int cb_libc_rmdir(const char *path);
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
int cb_libc_isdigit(int character);
int cb_libc_isspace(int character);
const char *cb_libc_getprogname(void);
void cb_libc_setprogname(const char *name);
int cb_libc_puts(const char *text);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(struct cb_libc_file *stream, const char *format, ...);
extern struct cb_libc_file *const cb_libc_stdin_stream;
extern struct cb_libc_file *const cb_libc_stdout_stream;
extern struct cb_libc_file *const cb_libc_stderr_stream;
size_t cb_libc_strlen(const char *text);
int cb_libc_strcmp(const char *left, const char *right);
char *cb_libc_strcpy(char *dest, const char *src);
void *cb_libc_memcpy(void *destination, const void *source, size_t count);
void *cb_libc_memmove(void *destination, const void *source, size_t count);
int cb_libc_memcmp(const void *left, const void *right, size_t count);
char *cb_libc_strchr(const char *text, int character);
char *cb_libc_strrchr(const char *text, int character);
void *cb_libc_memset(void *destination, int character, size_t count);
char *cb_libc_dirname_upstream(char *path);
char *cb_libc_dirname(char *path);
char *cb_libc_basename_upstream(char *path);
char *cb_libc_basename(char *path);
struct cb_libc_dir *cb_libc_opendir(const char *path);
struct dirent *cb_libc_readdir(struct cb_libc_dir *dirp);
int cb_libc_closedir(struct cb_libc_dir *dirp);

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
