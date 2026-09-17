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
/* Thin pass-throughs to bound_api->unlink/rmdir. unlink has been on the
   base table since before this project's optional-extension convention
   existed, so only a NULL check is needed. rmdir was appended by VFS-04,
   so it additionally needs the struct_size guard every appended field
   gets (matching cb_libc_opendir's family). cb_libc_stat/fstat/lstat are
   declared further below, alongside STAT-02's struct stat -- this file
   used to also declare an interim cb_stat_v1-shaped cb_libc_stat here for
   FTS-CORE-01's own use before STAT-02 existed; STAT-02's reconciliation
   of cb_fts.c replaced that need, so it is not re-declared here. */
int cb_libc_unlink(const char *path);
int cb_libc_rmdir(const char *path);

/*
 * The remaining declarations in this block exist so pinned rm.c compiles
 * in full. Every one of them backs a call site that is outside RM-01's
 * accepted matrix (documented in notes/iterations/RM-01.md: -P secure
 * overwrite, -W whiteout, check()'s auto-ask-on-unwritable heuristic) and
 * is never exercised by any test -- but "byte-for-byte unmodified pinned
 * source" means the file must still parse and link as a whole. Each is
 * implemented honestly for what it actually is, not stubbed to falsely
 * claim success: cb_off_t (*lseek is a real, always-available base ABI
 * op) and RAMFS's genuinely nothing-to-flush semantics for fsync/sync
 * (a no-op is CORRECT for an all-in-memory filesystem, not dishonest,
 * unlike a fake success for a capability that does not exist) get real
 * implementations; capabilities RAMFS truly does not have (whiteout,
 * a passwd/group database, permission-bit access checks, secure-erase
 * randomness) fail honestly or fall back to the same numeric-ID
 * rendering real BSD's own user_from_uid/group_from_gid use for any
 * uid/gid absent from the passwd/group database, which describes every
 * uid/gid here, not a special case.
 */
cb_off_t cb_libc_lseek(int descriptor, cb_off_t offset, int whence);
int cb_libc_fsync(int descriptor);
void cb_libc_sync(void);
uint32_t cb_libc_arc4random(void);
int cb_libc_access(const char *path, int mode);
int cb_libc_undelete(const char *path);
void cb_libc_strmode(uint32_t mode, char *buffer);
const char *cb_libc_user_from_uid(uint32_t uid, int nouser);
const char *cb_libc_group_from_gid(uint32_t gid, int nogroup);
/* Provisional placeholder -- see libc/include/signal.h's own comment. */
void (*cb_libc_signal(int sig, void (*handler)(int)))(int);

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
#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000
/* RM-01: see libc/include/sys/stat.h's own comment on S_IFWHT/S_ISWHT.
   The bit constant is needed here too, so cb_libc_strmode (compiled
   without libc/include on its path) can test for it directly. */
#define S_IFWHT 0160000

/*
 * POSIX struct stat definition, shared between cb_libc.c (which populates it)
 * and libc/include/sys/stat.h (which exposes it to ordinary source).
 * Types are standard integer types (uint64_t, uint32_t, int64_t, int32_t)
 * matching ino_t, mode_t, off_t, blksize_t, blkcnt_t.
 */
/*
 * st_uid/st_gid/st_dev added by RM-01. RAMFS has no per-node ownership or
 * multi-device concept at all (confirmed by reading struct cb_ramfs_node's
 * field list), so these are always 0 -- the same kind of reasonable,
 * explicitly-arbitrary sentinel STAT-02 already established for
 * st_blksize/st_blocks, not a claim about real file ownership. Needed for
 * rm.c's check()/rm_overwrite() to compile; both call sites are outside
 * the accepted matrix (see notes/iterations/RM-01.md) and never reached
 * by any test, but a plain, non-ABI-versioned struct stat carries none of
 * cb_stat_v1's struct_size/backward-compatibility concerns, so extending
 * it here does not risk the kind of duplicated-shape problem avoided by
 * not touching cb_stat_v1 itself.
 */
struct stat {
    uint64_t st_ino;
    uint32_t st_mode;
    int64_t st_size;
    int32_t st_blksize;
    int64_t st_blocks;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_dev;
};

int cb_libc_stat(const char *path, struct stat *stat_buf);
int cb_libc_fstat(int descriptor, struct stat *stat_buf);
int cb_libc_lstat(const char *path, struct stat *stat_buf);

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
