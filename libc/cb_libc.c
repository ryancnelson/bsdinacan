#include "cannedbsd/libc.h"

#ifndef EOF
#define EOF (-1)
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

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
                CB_LIBC_O_TRUNC | CB_LIBC_O_NONBLOCK;
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

static void translate_stat(const struct cb_stat_v1 *raw_stat, struct stat *stat_buf)
{
    uint32_t type_bits = 0;
    switch (raw_stat->type) {
    case CB_NODE_REGULAR:
    case CB_NODE_EXECUTABLE:
        type_bits = S_IFREG;
        break;
    case CB_NODE_DIRECTORY:
        type_bits = S_IFDIR;
        break;
    case CB_NODE_TERMINAL:
        type_bits = S_IFCHR;
        break;
    case CB_NODE_PIPE:
        type_bits = S_IFIFO;
        break;
    default:
        type_bits = 0;
        break;
    }

    stat_buf->st_ino = raw_stat->inode;
    stat_buf->st_mode = type_bits | (raw_stat->mode & 07777);
    stat_buf->st_rdev = 0;
    stat_buf->st_size = (int64_t)raw_stat->size;
    stat_buf->st_blksize = 1024; /* Arbitrary I/O buffer sizing hint for client stdio/cat */
    stat_buf->st_blocks = 0;    /* RAMFS allocates byte buffers; 0 allocated disk blocks */
    stat_buf->st_flags = 0;

    /* FS-STAT-01: real device/nlink/uid/gid/timestamps if the runtime's
       cb_stat_v1 carries them; an old runtime whose struct predates this
       append cannot honestly report them, so it keeps the same fixed
       sentinels this function always used (device 1, nlink 1, zero
       timestamps) rather than reading past what the caller actually
       populated. */
    if (raw_stat->struct_size >= CB_STAT_V1_METADATA_MIN_SIZE) {
        stat_buf->st_dev = raw_stat->device;
        stat_buf->st_nlink = raw_stat->nlink;
        stat_buf->st_uid = raw_stat->uid;
        stat_buf->st_gid = raw_stat->gid;
        stat_buf->st_atimespec.tv_sec = (int64_t)(raw_stat->atime_ms / 1000);
        stat_buf->st_atimespec.tv_nsec = (long)((raw_stat->atime_ms % 1000) * 1000000);
        stat_buf->st_mtimespec.tv_sec = (int64_t)(raw_stat->mtime_ms / 1000);
        stat_buf->st_mtimespec.tv_nsec = (long)((raw_stat->mtime_ms % 1000) * 1000000);
        stat_buf->st_ctimespec.tv_sec = (int64_t)(raw_stat->ctime_ms / 1000);
        stat_buf->st_ctimespec.tv_nsec = (long)((raw_stat->ctime_ms % 1000) * 1000000);
    } else {
        stat_buf->st_dev = 1;
        stat_buf->st_nlink = 1;
        stat_buf->st_uid = 0;
        stat_buf->st_gid = 0;
        stat_buf->st_atimespec.tv_sec = 0;
        stat_buf->st_atimespec.tv_nsec = 0;
        stat_buf->st_mtimespec.tv_sec = 0;
        stat_buf->st_mtimespec.tv_nsec = 0;
        stat_buf->st_ctimespec.tv_sec = 0;
        stat_buf->st_ctimespec.tv_nsec = 0;
    }
}

int cb_libc_stat(const char *path, struct stat *stat_buf)
{
    struct cb_stat_v1 raw_stat;
    int result;

    if (path == NULL || stat_buf == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }

    raw_stat.abi_version = CB_ABI_VERSION_V1;
    raw_stat.struct_size = sizeof(struct cb_stat_v1);

    result = bound_api->stat(path, &raw_stat);
    if (result < 0) {
        return -1;
    }

    translate_stat(&raw_stat, stat_buf);
    return 0;
}

int cb_libc_fstat(int descriptor, struct stat *stat_buf)
{
    struct cb_stat_v1 raw_stat;
    int result;

    if (stat_buf == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }

    raw_stat.abi_version = CB_ABI_VERSION_V1;
    raw_stat.struct_size = sizeof(struct cb_stat_v1);

    result = bound_api->fstat(descriptor, &raw_stat);
    if (result < 0) {
        return -1;
    }

    translate_stat(&raw_stat, stat_buf);
    return 0;
}

int cb_libc_lstat(const char *path, struct stat *stat_buf)
{
    return cb_libc_stat(path, stat_buf);
}

int cb_libc_rename(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }
    if (bound_api->struct_size < sizeof(struct cb_api_v1) ||
        bound_api->rename == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->rename(old_path, new_path);
}

int cb_libc_unlink(const char *path)
{
    if (path == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }
    if (bound_api->unlink == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->unlink(path);
}

/* rmdir was appended by VFS-04, past api_is_usable's checked struct_size
   boundary -- unlike unlink (present since the original base table), an
   old-table bind can genuinely be too small to reach this field at all,
   not just have it left NULL. Same shape as opendir_api_available above. */
static int rmdir_api_available(void)
{
    return bound_api->struct_size >=
               offsetof(struct cb_api_v1, rmdir) +
                   sizeof(bound_api->rmdir) &&
           bound_api->rmdir != NULL;
}

int cb_libc_rmdir(const char *path)
{
    if (path == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }
    if (!rmdir_api_available()) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->rmdir(path);
}

/* See the block comment in cannedbsd/libc.h introducing this group. */

cb_off_t cb_libc_lseek(int descriptor, cb_off_t offset, int whence)
{
    return bound_api->lseek(descriptor, offset, whence);
}

int cb_libc_fsync(int descriptor)
{
    /* RAMFS has no write-back cache: every write is already durable in
       the only "storage" this backend has, so there is never anything
       to flush. Returning success is a correct description of that
       fact, not a false claim about a capability that does not exist. */
    (void)descriptor;
    return 0;
}

void cb_libc_sync(void)
{
    /* Same reasoning as cb_libc_fsync, for every open file at once. */
}

uint32_t cb_libc_arc4random(void)
{
    /* NOT a cryptographic RNG. RM-01's only call site (rm -P's secure
       overwrite) is outside the accepted matrix and unreachable in
       practice: rm_overwrite()'s open() call already fails first, since
       O_SYNC/O_RSYNC/O_NOFOLLOW are not in cb_libc_open's known flag
       set. This exists only so the file links; nothing in the accepted
       matrix depends on its output being unpredictable. */
    static uint32_t state = 2463534242u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int cb_libc_access(const char *path, int mode)
{
    /* RAMFS never enforces mode bits for any operation -- open()/write()
       succeed regardless of st_mode's permission bits, matching this
       project's stated "permission enforcement deferred" policy. So
       "is this path accessible for R/W/X" is truthfully "yes, for
       anything that exists" on this backend, not a claim about
       permission-checking machinery that does not exist. Confirmed
       necessary, not assumed: an earlier ENOSYS-always version of this
       function was caught making check()'s "ask before removing an
       unwritable file" heuristic fire on every ordinary rm, inverting
       its intended default -- see notes/iterations/RM-01.md. */
    struct cb_stat_v1 probe;
    (void)mode;
    if (path == NULL) {
        bound_api->set_errno(CB_EFAULT);
        return -1;
    }
    if (bound_api->stat == NULL) {
        bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    probe.abi_version = CB_ABI_VERSION_V1;
    probe.struct_size = sizeof(probe);
    return bound_api->stat(path, &probe) < 0 ? -1 : 0;
}

int cb_libc_undelete(const char *path)
{
    /* RAMFS has no whiteout concept -- same conclusion FTS-CORE-01's
       design reached for FTS_WHITEOUT. No node this backend can ever
       produce satisfies S_ISWHT, so rm.c's own logic never calls this
       for a file it actually stat'd; it exists only so -W's branch
       compiles and fails honestly if ever reached some other way. */
    (void)path;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_fcpxattr(int from_descriptor, int to_descriptor)
{
    (void)from_descriptor;
    (void)to_descriptor;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

static void uint_to_decimal(uint32_t value, char *buffer)
{
    char digits[10];
    int count = 0;
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0)
        *buffer++ = digits[--count];
    *buffer = '\0';
}

/*
 * In fastcopy(), open(to, O_CREAT | O_TRUNC | O_WRONLY, sbp->st_mode) has
 * already created the destination file with the exact synthesized st_mode
 * bits (incorporating raw_stat->mode & 07777). Because the target descriptor
 * was already instantiated with the requested mode bits at creation time,
 * this descriptor-mode confirmation returns 0 truthfully.
 */
int cb_libc_fchmod(int descriptor, uint32_t mode)
{
    (void)descriptor;
    (void)mode;
    return 0;
}

int cb_libc_fchown(int descriptor, uint32_t uid, uint32_t gid)
{
    (void)descriptor;
    (void)uid;
    (void)gid;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_fchflags(int descriptor, uint32_t flags)
{
    (void)descriptor;
    (void)flags;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_futimes(int descriptor, const struct timeval *times)
{
    (void)descriptor;
    (void)times;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_utimes(const char *path, const struct timeval *times)
{
    (void)path;
    (void)times;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

/* PROVISIONAL PLACEHOLDER -- see libc/include/signal.h's own comment.
   Always fails (SIG_ERR, matching real POSIX signal()'s own failure
   return); never actually installs anything. Both rm.c and mv.c discard
   the return value, so this is a silent, honest no-op either way. */
void (*cb_libc_signal(int sig, void (*func)(int)))(int)
{
    (void)sig;
    (void)func;
    bound_api->set_errno(CB_ENOSYS);
    return (void (*)(int))-1;
}

int32_t cb_libc_vfork(void)
{
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_execl(const char *path, const char *arg0, ...)
{
    (void)path;
    (void)arg0;
    bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int32_t cb_libc_waitpid(int32_t pid, int *status, int options)
{
    (void)pid;
    (void)status;
    (void)options;
    bound_api->set_errno(CB_ECHILD);
    return -1;
}

int cb_libc_mkdir(const char *path, uint32_t mode)
{
    if (path == NULL) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_EFAULT);
        return -1;
    }
    if (bound_api == NULL || bound_api->mkdir == NULL) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_ENOSYS);
        return -1;
    }
    return bound_api->mkdir(path, mode);
}

void *cb_libc_mmap(void *addr, size_t len, int prot, int flags, int fd, cb_off_t offset)
{
    (void)addr;
    (void)len;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return (void *)-1;
}

int cb_libc_munmap(void *addr, size_t len)
{
    (void)addr;
    (void)len;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_madvise(void *addr, size_t len, int behav)
{
    (void)addr;
    (void)len;
    (void)behav;
    return 0;
}

int cb_libc_chmod(const char *path, uint32_t mode)
{
    (void)path;
    (void)mode;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_lchmod(const char *path, uint32_t mode)
{
    (void)path;
    (void)mode;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_chflags(const char *path, uint32_t flags)
{
    (void)path;
    (void)flags;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_lutimens(const char *path, const struct timespec times[2])
{
    (void)path;
    (void)times;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

uint32_t cb_libc_getuid(void)
{
    return 0;
}

static uint32_t current_umask = 022;

uint32_t cb_libc_umask(uint32_t numask)
{
    uint32_t old_mask = current_umask;
    current_umask = numask & 0777;
    return old_mask;
}

char *cb_libc_strncat(char *s1, const char *s2, size_t n)
{
    char *dest = s1;
    if (s1 == NULL || s2 == NULL)
        return s1;
    while (*dest != '\0')
        dest++;
    while (n > 0 && *s2 != '\0') {
        *dest++ = *s2++;
        n--;
    }
    *dest = '\0';
    return s1;
}

int cb_libc_link(const char *name1, const char *name2)
{
    (void)name1;
    (void)name2;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_symlink(const char *name1, const char *name2)
{
    (void)name1;
    (void)name2;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

cb_ssize_t cb_libc_readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_EINVAL);
    return -1;
}

int cb_libc_mkfifo(const char *path, uint32_t mode)
{
    (void)path;
    (void)mode;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    (void)path;
    (void)mode;
    (void)dev;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_lchown(const char *path, uint32_t uid, uint32_t gid)
{
    (void)path;
    (void)uid;
    (void)gid;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

int cb_libc_fcntl(int fd, int cmd, ...)
{
    (void)fd;
    (void)cmd;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

/* More complete than a plain rwx rendering: handles setuid/setgid/sticky
   bit overlays (s/S/t/T), which rm's own check() prompt formatting can
   actually display given the isatty()-always-true caveat recorded in
   notes/iterations/RM-01.md. */
void cb_libc_strmode(uint32_t mode, char *p)
{
    if (p == NULL)
        return;
    /* Inlined S_IS*(mode) tests: the S_IS* macros themselves live in
       libc/include/sys/stat.h, not on this translation unit's include
       path (matching every other cb_libc.c function that reads type
       bits, e.g. translate_stat's own switch above). */
    switch (mode & S_IFMT) {
    case S_IFDIR:  p[0] = 'd'; break;
    case S_IFCHR:  p[0] = 'c'; break;
    case S_IFBLK:  p[0] = 'b'; break;
    case S_IFREG:  p[0] = '-'; break;
    case S_IFLNK:  p[0] = 'l'; break;
    case S_IFSOCK: p[0] = 's'; break;
    case S_IFIFO:  p[0] = 'p'; break;
    case S_IFWHT:  p[0] = 'w'; break;
    default:       p[0] = '?'; break;
    }
    p[1] = (mode & S_IRUSR) ? 'r' : '-';
    p[2] = (mode & S_IWUSR) ? 'w' : '-';
    p[3] = (mode & S_ISUID) ? ((mode & S_IXUSR) ? 's' : 'S') : ((mode & S_IXUSR) ? 'x' : '-');
    p[4] = (mode & S_IRGRP) ? 'r' : '-';
    p[5] = (mode & S_IWGRP) ? 'w' : '-';
    p[6] = (mode & S_ISGID) ? ((mode & S_IXGRP) ? 's' : 'S') : ((mode & S_IXGRP) ? 'x' : '-');
    p[7] = (mode & S_IROTH) ? 'r' : '-';
    p[8] = (mode & S_IWOTH) ? 'w' : '-';
    p[9] = (mode & S_ISVTX) ? ((mode & S_IXOTH) ? 't' : 'T') : ((mode & S_IXOTH) ? 'x' : '-');
    p[10] = ' ';
    p[11] = '\0';
}

const char *cb_libc_user_from_uid(uint32_t uid, int nouser)
{
    /* No passwd database exists on this backend at all, so every uid is
       "not found" -- real BSD's own user_from_uid falls back to exactly
       this numeric rendering for any uid absent from the database,
       which describes every uid here, not a special case invented for
       this project. */
    static char buffer[16];
    if (nouser)
        return NULL;
    uint_to_decimal(uid, buffer);
    return buffer;
}

const char *cb_libc_group_from_gid(uint32_t gid, int nogroup)
{
    static char buffer[16];
    if (nogroup)
        return NULL;
    uint_to_decimal(gid, buffer);
    return buffer;
}

size_t cb_libc_strlcpy(char *dst, const char *src, size_t siz)
{
    size_t srclen;
    if (src == NULL)
        return 0;
    srclen = cb_libc_strlen(src);
    if (siz != 0 && dst != NULL) {
        size_t copylen = (srclen >= siz) ? (siz - 1) : srclen;
        cb_libc_memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}

/* cb_libc_strrchr is NOT defined here: it is the pinned NetBSD import
   (upstream/netbsd/common/lib/libc/string/strrchr.c, see UPSTREAM.md),
   archived into libcannedbsd.a via string.h's plain #define rename, the
   same treatment as its strchr sibling. A hand-written duplicate body
   here would be a genuine link-time duplicate-symbol conflict, not just
   redundant, and would abandon this project's established convention of
   pinning real upstream sources for standard library primitives instead
   of hand-rolling them. */

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

/* CAT-01: an old runtime whose stdio_state predates stdout_closed/
   stderr_closed cannot honestly report closure, so treat it as open --
   the struct_size guard below is what makes that fallback honest rather
   than a silent assumption. */
static int stdio_stream_closed(int descriptor)
{
    struct cb_stdio_state_v1 *state = stdio_state();
    if (state == NULL || state->struct_size < CB_STDIO_STATE_V1_CLOSED_MIN_SIZE)
        return 0;
    if (descriptor == 1)
        return state->stdout_closed;
    if (descriptor == 2)
        return state->stderr_closed;
    return 0;
}

int cb_libc_fclose(struct cb_libc_file *stream)
{
    struct cb_input_state_v1 *state;
    struct cb_libc_file *node, *previous;
    int result, error, saved_errno = bound_api->get_errno();
    if (stream == cb_libc_stdout_stream || stream == cb_libc_stderr_stream) {
        struct cb_stdio_state_v1 *stdio = stdio_state();
        int descriptor = (stream == cb_libc_stderr_stream) ? 2 : 1;
        if (stdio == NULL ||
            stdio->struct_size < CB_STDIO_STATE_V1_CLOSED_MIN_SIZE) {
            /* No way to record closure honestly on this runtime; keep
               the original honest rejection rather than claim a
               closure we cannot enforce. */
            bound_api->set_errno(CB_EINVAL);
            return EOF;
        }
        if (stdio_stream_closed(descriptor)) {
            bound_api->set_errno(CB_EINVAL);
            return EOF;
        }
        /* Output is unbuffered by design (STDIN-01/FWRITE-01): every
           write already reaches the descriptor immediately, so there is
           nothing to flush here. Marking the stream closed from the
           task's own perspective, rather than releasing descriptor 1/2
           immediately, is what keeps this success return truthful --
           write_all()/cb_libc_fwrite() now genuinely fail on it
           afterward, and the descriptor itself is reclaimed at normal
           task teardown like any other resource the task no longer
           references. */
        if (descriptor == 1)
            stdio->stdout_closed = 1;
        else
            stdio->stderr_closed = 1;
        bound_api->set_errno(saved_errno);
        return 0;
    }
    if (stream == NULL) {
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

int cb_libc_getchar(void)
{
    return cb_libc_getc(cb_libc_stdin_stream);
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
#if SIZE_MAX > INT64_MAX
        /* Tautological on ILP32 (e.g. Solaris 9 SPARC): a 32-bit
           size_t can never exceed INT64_MAX. Guarded, matching
           cb_libc_fwrite's identical guard below, to avoid an
           always-false-comparison diagnostic on those builds -- this
           is the exact class of warning already observed (unguarded)
           in the equivalent checks in src/core.c's api_read/api_write
           when building for a 32-bit target. */
        if ((uint64_t)request > (uint64_t)INT64_MAX)
            request = (size_t)INT64_MAX;
#endif
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

int cb_libc_isdigit(int character)
{
    return character >= '0' && character <= '9';
}

int cb_libc_isspace(int character)
{
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\v' || character == '\f' || character == '\r';
}

int cb_libc_isascii(int character)
{
    return (character >= 0 && character <= 0x7f);
}

int cb_libc_toascii(int character)
{
    return character & 0x7f;
}

int cb_libc_iscntrl(int character)
{
    return (character >= 0 && character <= 0x1f) || character == 0x7f;
}

long cb_libc_strtol(const char *nptr, char **endptr, int base)
{
    intmax_t val = cb_libc_strtoimax(nptr, endptr, base);
    if (val > LONG_MAX) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_ERANGE);
        return LONG_MAX;
    }
    if (val < LONG_MIN) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_ERANGE);
        return LONG_MIN;
    }
    return (long)val;
}

static int write_all(int descriptor, const char *text, size_t length)
{
    int saved_incoming_errno = bound_api->get_errno();
    if ((descriptor == 1 || descriptor == 2) &&
        stdio_stream_closed(descriptor)) {
        bound_api->set_errno(CB_EBADF);
        return -1;
    }
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
        } else {
            /* Bounded width 1-32 (literal or "*"-from-argument), an
               optional "-" left-justify flag or "'" thousands-separator
               flag (mutually exclusive in every real call site), an
               optional length modifier ("l"/"ll"), and %d/%u/%s -- see
               FORMAT-01-design.md's original %Nd/%Ns scope and its
               LS-02 addendum. LS-02 measured pinned ls/print.c's column
               layout ("%*llu ", "%-*s  ", "%*"PRIu64" ", "%'*llu " for
               -M) and demonstrated that scope insufficient for a second
               consumer -- extended here, not anticipated speculatively.
               Same immediate-CB_EINVAL policy as before for precision,
               any other flag, or any other conversion. */
            int left_justify = 0;
            int use_commas = 0;
            unsigned width = 0;
            int length_modifier = 0; /* 0 = none, 1 = 'l', 2 = "ll" */
            if (*cursor == '-') {
                left_justify = 1;
                ++cursor;
            }
            if (*cursor == '\'') {
                /* pinned ls/print.c's -M (thousands-separator) column
                   width prepass and rendering both use "%'*llu " /
                   "total %'llu\n" -- a real, reachable call site (-M
                   is not excluded from the accepted matrix), so this
                   groups digits by 3 for real rather than leaving -M
                   silently incomplete (ls.c voids every printf() return
                   value here, so an EINVAL would corrupt output rather
                   than visibly fail). */
                use_commas = 1;
                ++cursor;
            }
            if (*cursor == '*') {
                int argument_width = va_arg(arguments, int);
                if (argument_width < 0 || argument_width > 32) {
                    bound_api->set_errno(CB_EINVAL);
                    return -1;
                }
                width = (unsigned)argument_width;
                ++cursor;
            } else if (*cursor >= '1' && *cursor <= '9') {
                do {
                    unsigned digit = (unsigned)(*cursor - '0');
                    if (width > (32U - digit) / 10U) {
                        bound_api->set_errno(CB_EINVAL);
                        return -1;
                    }
                    width = width * 10U + digit;
                    ++cursor;
                } while (*cursor >= '0' && *cursor <= '9');
            }
            if (*cursor == 'l') {
                length_modifier = 1;
                ++cursor;
                if (*cursor == 'l') {
                    length_modifier = 2;
                    ++cursor;
                }
            }
            if (left_justify && *cursor != 's') {
                /* Only ls's user/group/flags columns need left-justify,
                   and only on %s; no signed/unsigned consumer does. */
                bound_api->set_errno(CB_EINVAL);
                return -1;
            }
            if (use_commas && *cursor != 'u') {
                /* Real ls.c call sites only ever pair "'" with %llu. */
                bound_api->set_errno(CB_EINVAL);
                return -1;
            }
            if (*cursor == 'd' || *cursor == 'u') {
                int is_unsigned = (*cursor == 'u');
                long long svalue = 0;
                unsigned long long magnitude;
                int negative = 0;
                /* 2^3 < 10: ceil(bits/3) bounds decimal digits for the
                   widest length modifier (long long); reserve a sign. */
                char number[(sizeof(long long) * CHAR_BIT + 2) / 3 + 2];
                /* Grouped copy: the digits above plus a comma every 3
                   digits (at most 6 for a 64-bit value) -- built only
                   when use_commas is set. */
                char grouped[sizeof(number) + 8];
                char *end = number + sizeof(number);
                char *digits = end;
                size_t length;
                if (is_unsigned) {
                    if (length_modifier == 2)
                        magnitude = va_arg(arguments, unsigned long long);
                    else if (length_modifier == 1)
                        magnitude = va_arg(arguments, unsigned long);
                    else
                        magnitude = va_arg(arguments, unsigned int);
                } else {
                    if (length_modifier == 2)
                        svalue = va_arg(arguments, long long);
                    else if (length_modifier == 1)
                        svalue = va_arg(arguments, long);
                    else
                        svalue = va_arg(arguments, int);
                    negative = svalue < 0;
                    magnitude = negative ?
                        (unsigned long long)0 - (unsigned long long)svalue :
                        (unsigned long long)svalue;
                }
                do {
                    *--digits = (char)('0' + magnitude % 10U);
                    magnitude /= 10U;
                } while (magnitude != 0);
                if (negative)
                    *--digits = '-';
                length = (size_t)(end - digits);
                if (use_commas && length > 3) {
                    char *write_ptr = grouped + sizeof(grouped);
                    size_t digit_count = 0;
                    const char *read_ptr = end;
                    *--write_ptr = '\0'; /* placeholder; not counted */
                    while (read_ptr > digits) {
                        --read_ptr;
                        *--write_ptr = *read_ptr;
                        ++digit_count;
                        if (digit_count % 3 == 0 && read_ptr > digits)
                            *--write_ptr = ',';
                    }
                    digits = write_ptr;
                    length = (size_t)(grouped + sizeof(grouped) - 1 - write_ptr);
                }
                while (width > length) {
                    if (add_output(descriptor, " ", 1, &total) < 0)
                        return -1;
                    --width;
                }
                if (add_output(descriptor, digits, length, &total) < 0)
                    return -1;
                ++cursor;
            } else if (*cursor == 's') {
                const char *text = va_arg(arguments, const char *);
                size_t length;
                if (length_modifier != 0) {
                    bound_api->set_errno(CB_EINVAL);
                    return -1;
                }
                if (text == NULL)
                    text = "(null)";
                length = cb_libc_strlen(text);
                /* Right-justify by default (pad if shorter, never
                   truncate if already wider); left-justify moves the
                   padding after the text instead. */
                if (!left_justify) {
                    while (width > length) {
                        if (add_output(descriptor, " ", 1, &total) < 0)
                            return -1;
                        --width;
                    }
                }
                if (add_output(descriptor, text, length, &total) < 0)
                    return -1;
                while (width > length) {
                    if (add_output(descriptor, " ", 1, &total) < 0)
                        return -1;
                    --width;
                }
                ++cursor;
            } else {
                bound_api->set_errno(CB_EINVAL);
                return -1;
            }
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

/* LS-02: appends up to `remaining` bytes of `text` into `*cursor`,
   advancing both, always leaving room for the eventual NUL snprintf
   writes on return -- mirrors real snprintf's "would have written"
   contract via `*total`, which is not clamped to the buffer size. */
static void snprintf_append(char **cursor, size_t *remaining, int *total,
                            const char *text, size_t length)
{
    size_t copy = length;
    if (copy > *remaining)
        copy = *remaining;
    if (copy > 0) {
        cb_libc_memcpy(*cursor, text, copy);
        *cursor += copy;
        *remaining -= copy;
    }
    *total += (int)length;
}

/* LS-02: pinned ls/print.c's column-width prepass calls snprintf with
   exactly bare %s/%d/%u/%lld/%llu (no width, no left-justify, no other
   conversion) -- see the exact call sites grepped for this ID. A
   dedicated, buffer-writing formatter rather than routing through
   format_output's descriptor-based one, since the two sinks (a real fd
   vs. a caller's buffer) don't share enough to be worth unifying for
   this narrow, already-measured surface. */
int cb_libc_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    const char *cursor = format;
    char *out = buffer;
    size_t remaining = size > 0 ? size - 1 : 0;
    int total = 0;

    if (format == NULL || (buffer == NULL && size != 0)) {
        bound_api->set_errno(CB_EINVAL);
        return -1;
    }
    va_start(arguments, format);
    while (*cursor != '\0') {
        const char *literal = cursor;
        while (*cursor != '\0' && *cursor != '%')
            ++cursor;
        snprintf_append(&out, &remaining, &total, literal,
                        (size_t)(cursor - literal));
        if (*cursor == '\0')
            break;
        ++cursor;
        if (*cursor == '%') {
            snprintf_append(&out, &remaining, &total, "%", 1);
            ++cursor;
            continue;
        }
        {
            int length_modifier = 0;
            if (*cursor == 'l') {
                length_modifier = 1;
                ++cursor;
                if (*cursor == 'l') {
                    length_modifier = 2;
                    ++cursor;
                }
            }
            if (*cursor == 's') {
                const char *text;
                if (length_modifier != 0) {
                    va_end(arguments);
                    bound_api->set_errno(CB_EINVAL);
                    return -1;
                }
                text = va_arg(arguments, const char *);
                if (text == NULL)
                    text = "(null)";
                snprintf_append(&out, &remaining, &total, text,
                                cb_libc_strlen(text));
                ++cursor;
            } else if (*cursor == 'd' || *cursor == 'u') {
                int is_unsigned = (*cursor == 'u');
                long long svalue = 0;
                unsigned long long magnitude;
                int negative = 0;
                char number[(sizeof(long long) * CHAR_BIT + 2) / 3 + 2];
                char *end = number + sizeof(number);
                char *digits = end;
                if (is_unsigned) {
                    if (length_modifier == 2)
                        magnitude = va_arg(arguments, unsigned long long);
                    else if (length_modifier == 1)
                        magnitude = va_arg(arguments, unsigned long);
                    else
                        magnitude = va_arg(arguments, unsigned int);
                } else {
                    if (length_modifier == 2)
                        svalue = va_arg(arguments, long long);
                    else if (length_modifier == 1)
                        svalue = va_arg(arguments, long);
                    else
                        svalue = va_arg(arguments, int);
                    negative = svalue < 0;
                    magnitude = negative ?
                        (unsigned long long)0 - (unsigned long long)svalue :
                        (unsigned long long)svalue;
                }
                do {
                    *--digits = (char)('0' + magnitude % 10U);
                    magnitude /= 10U;
                } while (magnitude != 0);
                if (negative)
                    *--digits = '-';
                snprintf_append(&out, &remaining, &total, digits,
                                (size_t)(end - digits));
                ++cursor;
            } else {
                va_end(arguments);
                bound_api->set_errno(CB_EINVAL);
                return -1;
            }
        }
    }
    va_end(arguments);
    if (size > 0)
        *out = '\0';
    return total;
}

/* LS-02: ls -h (SI-scaled sizes) is outside the accepted matrix -- see
   util.h's own comment. Always fails; ls.c's own err(1,...) at its one
   call site makes -h fail loudly rather than print a fabricated size. */
int cb_libc_humanize_number(char *buffer, size_t length, int64_t quantity,
                            const char *suffix, int scale, int flags)
{
    (void)buffer;
    (void)length;
    (void)quantity;
    (void)suffix;
    (void)scale;
    (void)flags;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOSYS);
    return -1;
}

/* LS-02: single, immutable C locale (see wchar.h's own comment) -- a
   real 1:1 byte<->wide-character mapping, genuinely stateless (mbstate_t
   carries no real state to track), not a fabricated multibyte decoder.
   Consuming the terminating NUL returns 0 per POSIX's own mbrtowc
   contract, not 1. */
size_t cb_libc_mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps)
{
    (void)ps;
    if (s == NULL)
        return 0; /* stateless: "reset" always already holds */
    if (n == 0)
        return (size_t)-2;
    if (pwc != NULL)
        *pwc = (unsigned char)s[0];
    return s[0] == '\0' ? 0 : 1;
}

size_t cb_libc_wcrtomb(char *s, wchar_t wc, void *ps)
{
    (void)ps;
    if (s == NULL)
        return 1; /* stateless: the "reset" sequence is zero bytes wide */
    s[0] = (char)wc;
    return 1;
}

int cb_libc_iswprint(int wc)
{
    return wc >= 0x20 && wc <= 0x7E;
}

int cb_libc_wcwidth(wchar_t wc)
{
    if (wc == 0)
        return 0;
    return cb_libc_iswprint((int)wc) ? 1 : -1;
}

/* LS-02: values must match libc/include/vis.h exactly; not visible here
   since this file does not include the public vis.h. */
#define LS02_VIS_WHITE  0x02
#define LS02_VIS_CSTYLE 0x08

size_t cb_libc_strvis(char *dst, const char *src, int flags)
{
    char *start = dst;
    unsigned char c;
    while ((c = (unsigned char)*src++) != '\0') {
        int printable = c >= 0x21 && c <= 0x7E;
        int is_space = (c == ' ');
        int needs_escape;
        if (c == '\\') {
            *dst++ = '\\';
            *dst++ = '\\';
            continue;
        }
        if (is_space)
            needs_escape = (flags & LS02_VIS_WHITE) != 0;
        else
            needs_escape = !printable;
        if (!needs_escape) {
            *dst++ = (char)c;
            continue;
        }
        if (flags & LS02_VIS_CSTYLE) {
            char cstyle = 0;
            switch (c) {
            case '\n': cstyle = 'n'; break;
            case '\t': cstyle = 't'; break;
            case '\r': cstyle = 'r'; break;
            case '\b': cstyle = 'b'; break;
            case '\a': cstyle = 'a'; break;
            case '\f': cstyle = 'f'; break;
            case '\v': cstyle = 'v'; break;
            default: break;
            }
            if (cstyle != 0) {
                *dst++ = '\\';
                *dst++ = cstyle;
                continue;
            }
        }
        /* Octal fallback: \ooo, always three digits. */
        *dst++ = '\\';
        *dst++ = (char)('0' + ((c >> 6) & 07));
        *dst++ = (char)('0' + ((c >> 3) & 07));
        *dst++ = (char)('0' + (c & 07));
    }
    *dst = '\0';
    return (size_t)(dst - start);
}

/* LS-02: real getenv, not previously publicly wrapped (only used
   internally by cb_libc_setlocale before this). ls.c's only use is
   COLUMNS, an honest lookup against real task environ -- CANNEDBSD_ never
   sets it, so it is genuinely unset here, not fabricated absent. */
const char *cb_libc_getenv(const char *name)
{
    if (bound_api == NULL || bound_api->getenv == NULL)
        return NULL;
    return bound_api->getenv(name);
}

int cb_libc_atoi(const char *nptr)
{
    return (int)cb_libc_strtol(nptr, NULL, 10);
}

/* LS-02: real default path, not a stub -- see util.h's own comment. */
char *cb_libc_getbsize(int *headerlenp, long *blocksizep)
{
    static char label[] = "512";
    if (headerlenp != NULL)
        *headerlenp = (int)(sizeof(label) - 1);
    if (blocksizep != NULL)
        *blocksizep = 512;
    return label;
}

/* LS-02: st_flags is always 0 here (see util.h's own comment), so the
   only reachable case is "return the caller's default for zero flags",
   exactly matching real flags_to_string's own zero-flags behavior. */
char *cb_libc_flags_to_string(unsigned long flags, const char *def)
{
    static char empty[] = "";
    if (flags == 0)
        return (char *)def;
    return empty;
}

/* wall_clock_millis was appended by LS-02, past api_is_usable's checked
   struct_size boundary -- same shape as rmdir_api_available above. */
static int wall_clock_api_available(void)
{
    return bound_api->struct_size >=
               offsetof(struct cb_api_v1, wall_clock_millis) +
                   sizeof(bound_api->wall_clock_millis) &&
           bound_api->wall_clock_millis != NULL;
}

/* LS-02: real current time via the newly-exposed wall_clock_millis
   (see abi.h/core.c's own comments) -- an honest ENOSYS refusal on an
   old runtime that predates it, not a fabricated zero. */
uint32_t cb_libc_time(uint32_t *out)
{
    uint64_t millis;
    uint32_t seconds;
    if (!wall_clock_api_available()) {
        bound_api->set_errno(CB_ENOSYS);
        return (uint32_t)-1;
    }
    millis = bound_api->wall_clock_millis();
    seconds = (uint32_t)(millis / 1000U);
    if (out != NULL)
        *out = seconds;
    return seconds;
}

/* Howard Hinnant's civil_from_days (public domain): exact proleptic
   Gregorian civil date from a day count relative to 1970-01-01, valid
   for the entire range a uint32_t time_t can represent. Chosen over a
   naive year-loop for correctness across leap years without a lookup
   table. */
static void ls02_civil_from_days(long z, int *y, int *m, int *d)
{
    long era;
    unsigned doe, yoe, doy, mp, dd, mm;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    dd = doy - (153 * mp + 2) / 5 + 1;
    mm = mp + (mp < 10 ? 3 : (unsigned)-9);
    *y = (int)(yoe + (unsigned)era * 400) + (mm <= 2 ? 1 : 0);
    *m = (int)mm;
    *d = (int)dd;
}

/* LS-02: pinned ls/print.c's printtime() reads exact fixed character
   offsets out of ctime(3)'s 26-byte "Www Mmm dd hh:mm:ss yyyy\n\0"
   format, so this must match that layout exactly, not just be
   "a reasonable date string". */
char *cb_libc_ctime(const uint32_t *timer)
{
    static const char weekday_names[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char month_names[12][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static char buf[26];
    long total_seconds;
    long days, remainder;
    int weekday, hour, minute, second, year, month, day;

    if (timer == NULL)
        return NULL;
    total_seconds = (long)*timer;
    days = total_seconds / 86400;
    remainder = total_seconds % 86400;
    if (remainder < 0) {
        remainder += 86400;
        days -= 1;
    }
    hour = (int)(remainder / 3600);
    minute = (int)((remainder % 3600) / 60);
    second = (int)(remainder % 60);
    /* 1970-01-01 was a Thursday (index 4). */
    weekday = (int)(((days % 7) + 7 + 4) % 7);
    ls02_civil_from_days(days, &year, &month, &day);

    cb_libc_memcpy(buf, weekday_names[weekday], 3);
    buf[3] = ' ';
    cb_libc_memcpy(buf + 4, month_names[month - 1], 3);
    buf[7] = ' ';
    buf[8] = (char)(day < 10 ? ' ' : '0' + day / 10);
    buf[9] = (char)('0' + day % 10);
    buf[10] = ' ';
    buf[11] = (char)('0' + hour / 10);
    buf[12] = (char)('0' + hour % 10);
    buf[13] = ':';
    buf[14] = (char)('0' + minute / 10);
    buf[15] = (char)('0' + minute % 10);
    buf[16] = ':';
    buf[17] = (char)('0' + second / 10);
    buf[18] = (char)('0' + second % 10);
    buf[19] = ' ';
    buf[20] = (char)('0' + (year / 1000) % 10);
    buf[21] = (char)('0' + (year / 100) % 10);
    buf[22] = (char)('0' + (year / 10) % 10);
    buf[23] = (char)('0' + year % 10);
    buf[24] = '\n';
    buf[25] = '\0';
    return buf;
}

/* LS-02: honest failure for every request, including TIOCGWINSZ -- see
   sys/ioctl.h's own comment. No real terminal geometry exists here. */
int cb_libc_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    if (bound_api != NULL && bound_api->set_errno != NULL)
        bound_api->set_errno(CB_ENOTTY);
    return -1;
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

void cb_libc_warnx(const char *fmt, ...)
{
    int saved_error = bound_api->get_errno();
    const char *name = bound_api->getprogname();
    va_list arguments;
    if (name == NULL)
        name = "";
    write_all(2, name, cb_libc_strlen(name));
    write_all(2, ": ", 2);
    if (fmt != NULL) {
        va_start(arguments, fmt);
        format_output(2, fmt, arguments);
        va_end(arguments);
    }
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
    if (stream != NULL) {
        int descriptor = (stream == cb_libc_stderr_stream) ? 2 : 1;
        if (stdio_stream_closed(descriptor)) {
            bound_api->set_errno(CB_EBADF);
            return EOF;
        }
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

void cb_libc_clearerr(struct cb_libc_file *stream)
{
    if (stream == NULL)
        return;
    if (stream == cb_libc_stdout_stream || stream == cb_libc_stderr_stream) {
        struct cb_stdio_state_v1 *state = stdio_state();
        if (state != NULL) {
            if (stream == cb_libc_stdout_stream)
                state->stdout_error = 0;
            else
                state->stderr_error = 0;
        }
        return;
    }
    struct input_reference ref;
    if (resolve_input(stream, &ref) == 0) {
        *ref.eof = 0;
        *ref.error = 0;
    }
}

int cb_libc_fileno(struct cb_libc_file *stream)
{
    if (stream == NULL) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_EBADF);
        return -1;
    }
    if (stream == cb_libc_stdin_stream)
        return 0;
    if (stream == cb_libc_stdout_stream)
        return 1;
    if (stream == cb_libc_stderr_stream)
        return 2;
    struct input_reference ref;
    if (resolve_input(stream, &ref) < 0) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_EBADF);
        return -1;
    }
    return ref.descriptor;
}

void cb_libc_setbuf(struct cb_libc_file *stream, char *buf)
{
    if (stream == NULL) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_EINVAL);
        return;
    }
    if (buf != NULL) {
        if (bound_api != NULL && bound_api->set_errno != NULL)
            bound_api->set_errno(CB_ENOSYS);
        return;
    }
    if (stream != cb_libc_stdin_stream && stream != cb_libc_stdout_stream &&
        stream != cb_libc_stderr_stream) {
        struct input_reference ref;
        if (resolve_input(stream, &ref) < 0) {
            if (bound_api != NULL && bound_api->set_errno != NULL)
                bound_api->set_errno(CB_EBADF);
            return;
        }
    }
}


size_t cb_libc_fwrite(const void *buffer, size_t size, size_t count, struct cb_libc_file *stream)
{
    if (size == 0 || count == 0)
        return 0;

    if (stream != cb_libc_stdout_stream && stream != cb_libc_stderr_stream) {
        if (bound_api != NULL) bound_api->set_errno(CB_EINVAL);
        return 0;
    }

    struct cb_stdio_state_v1 *state = stdio_state();
    if (state == NULL || bound_api == NULL || bound_api->write == NULL) {
        if (bound_api != NULL) bound_api->set_errno(CB_ENOSYS);
        return 0;
    }

    if (size > SIZE_MAX / count) {
        bound_api->set_errno(CB_EOVERFLOW);
        return 0;
    }

    if (buffer == NULL) {
        bound_api->set_errno(CB_EINVAL);
        return 0;
    }

    int descriptor = (stream == cb_libc_stderr_stream) ? 2 : 1;
    if (stdio_stream_closed(descriptor)) {
        bound_api->set_errno(CB_EBADF);
        return 0;
    }
    int saved_incoming_errno = bound_api->get_errno();
    size_t total_bytes = size * count;
    size_t remaining = total_bytes;
    const char *text = (const char *)buffer;

    while (remaining != 0) {
        size_t chunk = remaining;
#if SIZE_MAX > INT64_MAX
        if ((uint64_t)chunk > (uint64_t)INT64_MAX) {
            chunk = (size_t)INT64_MAX;
        }
#endif

        cb_ssize_t written = bound_api->write(descriptor, text, chunk);
        if (written < 0) {
            int actual_error = bound_api->get_errno();
            mark_stdio_error(descriptor);
            bound_api->set_errno(actual_error);
            break;
        }
        if (written == 0 || (uint64_t)written > (uint64_t)chunk) {
            mark_stdio_error(descriptor);
            bound_api->set_errno(CB_EIO);
            break;
        }

        text += (size_t)written;
        remaining -= (size_t)written;
    }

    if (remaining == 0) {
        bound_api->set_errno(saved_incoming_errno);
    }

    return (total_bytes - remaining) / size;
}
