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
    /* CAT-01: FCNTL-01 declared O_NONBLOCK (fcntl.h) as a compile-time
       symbol only, for cat -f's open() call, never wiring it into
       translate_open_flags() -- a compilation-only gap, not a reasoned
       rejection like O_SYNC/O_RSYNC/O_NOFOLLOW in RM-01. Accepted and
       silently discarded rather than translated: RAMFS opens/reads/
       writes never block in the first place, so "this open will not
       block" is unconditionally true here regardless of this flag,
       making acceptance honest rather than a fabricated capability. */
    CB_LIBC_O_NONBLOCK = 0x0004,
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
/*
 * unlink/rmdir/access/strmode/user_from_uid/group_from_gid/signal and
 * cb_libc_stat/fstat/lstat are declared further below, alongside
 * STAT-02/MV-01's struct stat and their own fuller cluster of metadata
 * operations (cb_libc_rename/fcpxattr/fchmod/fchown/fchflags/etc.) --
 * MV-01 landed needing nearly the same POSIX-utility-heuristic surface
 * RM-01 does (both rm's and mv's own interactive-confirmation checks use
 * access/strmode/user_from_uid/group_from_gid; both use signal/SIGINFO
 * for ^T progress reporting), so this file used to also carry a second,
 * now-redundant copy of those declarations here; merged down to one.
 * lseek/fsync/sync/arc4random/undelete are RM-01-only (rm -P's macros),
 * declared here since MV-01 never needed them:
 *
 * cb_off_t (*)lseek is a real, always-available base ABI op, and RAMFS's
 * genuinely nothing-to-flush semantics make fsync/sync's success a
 * correct description of this backend, not a claimed capability. Both
 * get real implementations. arc4random/undelete back capabilities this
 * backend genuinely does not have (secure-erase randomness, whiteout)
 * and fail/are-inert honestly instead. See notes/iterations/RM-01.md.
 */
cb_off_t cb_libc_lseek(int descriptor, cb_off_t offset, int whence);
int cb_libc_fsync(int descriptor);
void cb_libc_sync(void);
uint32_t cb_libc_arc4random(void);
int cb_libc_undelete(const char *path);

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
intmax_t cb_libc_strtoimax(const char *nptr, char **endptr, int base);
long cb_libc_strtol(const char *nptr, char **endptr, int base);
int cb_libc_isdigit(int character);
int cb_libc_isspace(int character);
int cb_libc_isascii(int character);
int cb_libc_toascii(int character);
int cb_libc_iscntrl(int character);
const char *cb_libc_getprogname(void);
void cb_libc_setprogname(const char *name);
int cb_libc_puts(const char *text);
int cb_libc_printf(const char *format, ...);
int cb_libc_fprintf(struct cb_libc_file *stream, const char *format, ...);
int cb_libc_snprintf(char *buffer, size_t size, const char *format, ...);
int cb_libc_ioctl(int fd, unsigned long request, ...);
int cb_libc_humanize_number(char *buffer, size_t length, int64_t quantity,
                            const char *suffix, int scale, int flags);
/* wchar_t is a compiler builtin, already visible here via <stddef.h>
   (included transitively through cannedbsd/abi.h) -- spelled directly
   so this declaration is identical to libc/include/wchar.h's, not just
   convertible to it (an int/wchar_t mismatch is a real conflicting-type
   error on targets where wchar_t is unsigned, e.g. this project's own
   Alpine/musl build). mbstate_t is this project's own opaque struct
   (see wchar.h) and not visible here, so it stays spelled as void *;
   wint_t (see wctype.h) is typedef'd equal to int, so cb_libc_iswprint's
   plain int spelling here already matches it exactly. */
size_t cb_libc_mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps);
size_t cb_libc_wcrtomb(char *s, wchar_t wc, void *ps);
int cb_libc_iswprint(int wc);
int cb_libc_wcwidth(wchar_t wc);
size_t cb_libc_strvis(char *dst, const char *src, int flags);
const char *cb_libc_getenv(const char *name);
int cb_libc_atoi(const char *nptr);
char *cb_libc_getbsize(int *headerlenp, long *blocksizep);
char *cb_libc_flags_to_string(unsigned long flags, const char *def);
/* LS-02: time_t is spelled uint32_t here, matching st_atimespec.tv_sec's
   existing type (see struct timespec/struct stat above), since this
   internal header predates and does not itself define the public
   time_t typedef (see libc/include/time.h). */
uint32_t cb_libc_time(uint32_t *out);
char *cb_libc_ctime(const uint32_t *timer);
extern struct cb_libc_file *const cb_libc_stdin_stream;
extern struct cb_libc_file *const cb_libc_stdout_stream;
extern struct cb_libc_file *const cb_libc_stderr_stream;
void cb_libc_clearerr(struct cb_libc_file *stream);
int cb_libc_fileno(struct cb_libc_file *stream);
void cb_libc_setbuf(struct cb_libc_file *stream, char *buf);
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

#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

/* LS-02: pinned ls/print.c's -h sizing multiplies st_blocks by this to
   get bytes; the real BSD constant (the fixed unit st_blocks counts in),
   not a value this project invented. */
#define S_BLKSIZE 512

#define S_IRWXU 0000700
#define S_IRUSR 0000400
#define S_IWUSR 0000200
#define S_IXUSR 0000100

#define S_IRWXG 0000070
#define S_IRGRP 0000040
#define S_IWGRP 0000020
#define S_IXGRP 0000010

#define S_IRWXO 0000007
#define S_IROTH 0000004
#define S_IWOTH 0000002
#define S_IXOTH 0000001

#ifndef _STRUCT_TIMESPEC_DECLARED
struct timespec {
    uint32_t tv_sec;
    long tv_nsec;
};
#define _STRUCT_TIMESPEC_DECLARED
#endif

/*
 * POSIX struct stat definition, shared between cb_libc.c (which populates it)
 * and libc/include/sys/stat.h (which exposes it to ordinary source).
 * Types are standard integer types (uint64_t, uint32_t, int64_t, int32_t)
 * matching ino_t, mode_t, off_t, blksize_t, blkcnt_t.
 */
struct stat {
    uint32_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int64_t st_size;
    struct timespec st_atimespec;
    struct timespec st_mtimespec;
    struct timespec st_ctimespec;
    int32_t st_blksize;
    int64_t st_blocks;
    uint32_t st_flags;
};

#define st_atime st_atimespec.tv_sec
#define st_mtime st_mtimespec.tv_sec
#define st_ctime st_ctimespec.tv_sec
/* LS-02: pinned ls/cmp.c's MTIMENSEC_CMP uses the legacy flat NetBSD
   nanosecond field names alongside the POSIX timespec ones. */
#define st_atimensec st_atimespec.tv_nsec
#define st_mtimensec st_mtimespec.tv_nsec
#define st_ctimensec st_ctimespec.tv_nsec

struct timeval;

int cb_libc_stat(const char *path, struct stat *stat_buf);
int cb_libc_fstat(int descriptor, struct stat *stat_buf);
int cb_libc_lstat(const char *path, struct stat *stat_buf);
int cb_libc_rename(const char *old_path, const char *new_path);
int cb_libc_unlink(const char *path);
int cb_libc_rmdir(const char *path);
int cb_libc_mkdir(const char *path, uint32_t mode);
int cb_libc_access(const char *path, int mode);
int cb_libc_fcpxattr(int from_descriptor, int to_descriptor);
void cb_libc_warnx(const char *fmt, ...);
size_t cb_libc_strlcpy(char *dst, const char *src, size_t siz);
int cb_libc_getchar(void);
void cb_libc_strmode(uint32_t mode, char *p);
const char *cb_libc_user_from_uid(uint32_t uid, int nouser);
const char *cb_libc_group_from_gid(uint32_t gid, int nogroup);
int cb_libc_fchmod(int descriptor, uint32_t mode);
int cb_libc_fchown(int descriptor, uint32_t uid, uint32_t gid);
int cb_libc_fchflags(int descriptor, uint32_t flags);
int cb_libc_futimes(int descriptor, const struct timeval *times);
int cb_libc_utimes(const char *path, const struct timeval *times);
void (*cb_libc_signal(int sig, void (*func)(int)))(int);
int32_t cb_libc_vfork(void);
int cb_libc_execl(const char *path, const char *arg0, ...);
int32_t cb_libc_waitpid(int32_t pid, int *status, int options);
void *cb_libc_mmap(void *addr, size_t len, int prot, int flags, int fd, cb_off_t offset);
int cb_libc_munmap(void *addr, size_t len);
int cb_libc_madvise(void *addr, size_t len, int behav);
int cb_libc_chmod(const char *path, uint32_t mode);
int cb_libc_lchmod(const char *path, uint32_t mode);
int cb_libc_chflags(const char *path, uint32_t flags);
int cb_libc_lutimens(const char *path, const struct timespec times[2]);
uint32_t cb_libc_getuid(void);
uint32_t cb_libc_umask(uint32_t numask);
char *cb_libc_strncat(char *s1, const char *s2, size_t n);
int cb_libc_link(const char *name1, const char *name2);
int cb_libc_symlink(const char *name1, const char *name2);
cb_ssize_t cb_libc_readlink(const char *path, char *buf, size_t bufsiz);
int cb_libc_mkfifo(const char *path, uint32_t mode);
int cb_libc_mknod(const char *path, uint32_t mode, uint32_t dev);
int cb_libc_lchown(const char *path, uint32_t uid, uint32_t gid);
int cb_libc_fcntl(int fd, int cmd, ...);

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
