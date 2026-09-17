#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/param.h>
#include <fts.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static FILE *first, *second;
void *stream_probe_handle(unsigned index) { return index == 0 ? first : second; }

int main(int argc, char **argv)
{
    const char *mode;
    if (argc != 2) return 90;
    mode = argv[1];
    if (strcmp(mode, "cp-stub-probe") == 0) {
        char buf[64];
        struct timespec ts[2] = {{0, 0}, {0, 0}};
        struct stat sb;

        /* Identity and umask */
        if (getuid() != 0) return 101;
        if (umask(077) != 022) return 102;
        if (umask(022) != 077) return 103;

        /* Honest ENOSYS / EINVAL failures */
        errno = 0;
        if (chmod("/tmp/foo", 0644) != -1 || errno != ENOSYS) return 104;
        errno = 0;
        if (lchmod("/tmp/foo", 0644) != -1 || errno != ENOSYS) return 105;
        errno = 0;
        if (chflags("/tmp/foo", 0) != -1 || errno != ENOSYS) return 106;
        errno = 0;
        if (lchown("/tmp/foo", 0, 0) != -1 || errno != ENOSYS) return 107;
        errno = 0;
        if (lutimens("/tmp/foo", ts) != -1 || errno != ENOSYS) return 108;
        errno = 0;
        if (link("/tmp/a", "/tmp/b") != -1 || errno != ENOSYS) return 109;
        errno = 0;
        if (symlink("/tmp/a", "/tmp/b") != -1 || errno != ENOSYS) return 110;
        errno = 0;
        if (readlink("/tmp/a", buf, sizeof(buf)) != -1 || errno != EINVAL) return 111;
        errno = 0;
        if (mkfifo("/tmp/fifo", 0644) != -1 || errno != ENOSYS) return 112;
        errno = 0;
        if (mknod("/tmp/nod", 0644, 0) != -1 || errno != ENOSYS) return 113;

        /* strncat helper */
        strcpy(buf, "hello");
        if (strncat(buf, " world!", 3) != buf || strcmp(buf, "hello wo") != 0) return 114;
        if (strncat(buf, "rld", 10) != buf || strcmp(buf, "hello world") != 0) return 115;

        /* Constants and stat timestamp macros */
        if (FTS_ROOTLEVEL != 0) return 116;
        if (PATH_MAX != 1024) return 117;
        if (MAXBSIZE != 65536) return 118;

        if (stat("/", &sb) != 0) return 119;
        if (sb.st_atimespec.tv_sec != 0 || sb.st_atimespec.tv_nsec != 0) return 120;
        if (sb.st_mtimespec.tv_sec != 0 || sb.st_mtimespec.tv_nsec != 0) return 121;
        if (sb.st_ctimespec.tv_sec != 0 || sb.st_ctimespec.tv_nsec != 0) return 122;
        if (sb.st_atime != 0 || sb.st_mtime != 0 || sb.st_ctime != 0) return 123;

        return 0;
    }
    if (strcmp(mode, "mman-probe") == 0) {
        void *p;
        errno = 0;
        p = mmap(NULL, 1024, PROT_READ, MAP_SHARED | MAP_FILE, -1, 0);
        if (p != MAP_FAILED || errno != ENOSYS) return 80;
        errno = 0;
        if (munmap((void *)0x1000, 1024) != -1 || errno != ENOSYS) return 81;
        if (madvise((void *)0x1000, 1024, MADV_SEQUENTIAL) != 0) return 82;
        return 0;
    }
    if (strcmp(mode, "mkdir-probe") == 0) {
        struct stat sb;
        if (mkdir("/tmp/libc-mkdir-dir", 0755) != 0) return 83;
        if (stat("/tmp/libc-mkdir-dir", &sb) != 0 || !S_ISDIR(sb.st_mode)) return 84;
        if (mkdir("/tmp/libc-mkdir-dir", 0755) != -1 || errno != EEXIST) return 85;
        if (mkdir(NULL, 0755) != -1 || errno != EFAULT) return 86;
        if (rmdir("/tmp/libc-mkdir-dir") != 0) return 87;
        return 0;
    }
    if (strcmp(mode, "default-mode") == 0) {
        int fd = open("/tmp/default-mode", O_CREAT | O_WRONLY, DEFFILEMODE);
        if (fd != 3) { if (fd >= 0) close(fd); return 34; }
        return close(fd) == 0 ? 0 : 35;
    }
    if (strcmp(mode, "stat-probe") == 0) {
        struct stat sb, fsb;
        int fd, pipe_fds[2];

        /* Pathname stat on regular file */
        if (stat("/tmp/stream-input", &sb) != 0) return 60;
        if (!S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode) ||
            S_ISCHR(sb.st_mode) || S_ISFIFO(sb.st_mode)) return 61;
        if ((sb.st_mode & 0777) != 0600 || sb.st_size != 3) return 62;
        if (sb.st_blksize != 1024 || sb.st_blocks != 0) return 63;
        if (sb.st_ino == 0) return 64;

        /* lstat on regular file */
        if (lstat("/tmp/stream-input", &fsb) != 0) return 65;
        if (fsb.st_ino != sb.st_ino || fsb.st_mode != sb.st_mode ||
            fsb.st_size != sb.st_size) return 66;

        /* Descriptor fstat on regular file */
        fd = open("/tmp/stream-input", O_RDONLY, 0);
        if (fd < 0) return 67;
        if (fstat(fd, &fsb) != 0) { close(fd); return 68; }
        if (fsb.st_ino != sb.st_ino || fsb.st_mode != sb.st_mode ||
            fsb.st_size != sb.st_size || fsb.st_blksize != sb.st_blksize ||
            fsb.st_blocks != sb.st_blocks) { close(fd); return 69; }
        if (close(fd) != 0) return 70;

        /* fstat on closed descriptor */
        if (fstat(fd, &fsb) != -1 || errno != EBADF) return 71;

        /* Pathname stat on directory */
        if (stat("/", &sb) != 0) return 72;
        if (!S_ISDIR(sb.st_mode) || S_ISREG(sb.st_mode) ||
            S_ISCHR(sb.st_mode) || S_ISFIFO(sb.st_mode)) return 73;

        /* Pathname stat on non-existent file */
        if (stat("/no/such/file", &sb) != -1 || errno != ENOENT) return 74;

        /* Pathname stat with NULL path / stat buffer */
        if (stat(NULL, &sb) != -1 || errno != EFAULT) return 75;
        if (stat("/", NULL) != -1 || errno != EFAULT) return 76;
        if (fstat(0, NULL) != -1 || errno != EFAULT) return 77;

        /* fstat on pipe */
        if (pipe(pipe_fds) != 0) return 78;
        if (fstat(pipe_fds[0], &fsb) != 0) return 79;
        if (!S_ISFIFO(fsb.st_mode) || S_ISREG(fsb.st_mode) ||
            S_ISDIR(fsb.st_mode) || S_ISCHR(fsb.st_mode)) return 80;
        if (close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0) return 81;

        /* fstat on terminal / stdin (if chr) */
        if (fstat(0, &fsb) == 0 && S_ISCHR(fsb.st_mode)) {
            if (S_ISREG(fsb.st_mode) || S_ISDIR(fsb.st_mode) ||
                S_ISFIFO(fsb.st_mode)) return 82;
        }

        return 0;
    }
    if (strcmp(mode, "open-one") == 0 || strcmp(mode, "open-two") == 0) {
        errno = EPIPE;
        first = fopen("/tmp/stream-input", "r");
        if (first == NULL || errno != EPIPE || feof(first) || ferror(first)) return 1;
        if (strcmp(mode, "open-two") == 0) {
            second = fopen("/tmp/stream-input", "rb");
            if (second == NULL || first == second || errno != EPIPE ||
                feof(second) || ferror(second)) return 2;
        }
        return 0;
    }
    if (strcmp(mode, "read-two") == 0) {
        errno = EPIPE;
        if (getc(first) != 0 || getc(first) != 255 || getc(first) != 'A' ||
            feof(first) || ferror(first) || errno != EPIPE) return 3;
        if (getc(first) != EOF || !feof(first) || ferror(first) || errno != EPIPE)
            return 4;
        if (getc(second) != 0 || feof(second) || ferror(second) || errno != EPIPE)
            return 5;
        return 0;
    }
    if (strcmp(mode, "read-one") == 0) {
        errno = EPIPE;
        return getc(first) == 0 && !feof(first) && !ferror(first) &&
               errno == EPIPE ? 0 : 6;
    }
    if (strcmp(mode, "close-one") == 0 || strcmp(mode, "close-two") == 0) {
        errno = EPIPE;
        if (fclose(first) != 0 || errno != EPIPE) return 7;
        /* Bounded invalid-use diagnostic, before any allocator address reuse. */
        if (fclose(first) != EOF || errno != EINVAL) return 8;
        if (strcmp(mode, "close-two") == 0) {
            errno = EPIPE;
            if (fclose(second) != 0 || errno != EPIPE) return 9;
        }
        return 0;
    }
    if (strcmp(mode, "invalid") == 0) {
        static const char *modes[] = {"", "w", "a", "r+", "rb+", "rbb", "R"};
        unsigned i;
        for (i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i)
            if (fopen("/tmp/stream-input", modes[i]) != NULL || errno != EINVAL)
                return 10;
        if (fopen(NULL, "r") != NULL || errno != EINVAL ||
            fopen("/tmp/stream-input", NULL) != NULL || errno != EINVAL) return 11;
        if (fclose(NULL) != EOF || errno != EINVAL || fclose(stdout) != EOF ||
            errno != EINVAL || fclose(stderr) != EOF || errno != EINVAL) return 12;
        if (fclose((FILE *)1) != EOF || errno != EINVAL ||
            getc((FILE *)1) != EOF || errno != EINVAL ||
            feof((FILE *)1) || errno != EINVAL ||
            !ferror((FILE *)1) || errno != EINVAL) return 13;
        return !feof(stdin) && !ferror(stdin) && !ferror(stdout) &&
               !ferror(stderr) ? 0 : 14;
    }
    if (strcmp(mode, "missing-directory") == 0) {
        if (fopen("/no-such-stream", "r") != NULL || errno != ENOENT) return 15;
        return fopen("/", "rb") == NULL && errno == EISDIR ? 0 : 16;
    }
    if (strcmp(mode, "foreign") == 0) {
        FILE *own;
        if (fclose(first) != EOF || errno != EINVAL || getc(first) != EOF ||
            errno != EINVAL || feof(first) || errno != EINVAL ||
            !ferror(first) || errno != EINVAL) return 17;
        if (feof(stdin) || ferror(stdin)) return 18;
        own = fopen("/tmp/stream-input", "rb");
        if (own == NULL || getc(own) != 0 || fclose(own) != 0) return 19;
        return 0;
    }
    if (strcmp(mode, "close-stdin") == 0) {
        errno = EPIPE;
        if (fclose(stdin) != 0 || errno != EPIPE) return 20;
        return 0;
    }
    if (strcmp(mode, "closed-stdin") == 0) {
        if (fclose(stdin) != EOF || errno != EINVAL || getc(stdin) != EOF ||
            errno != EINVAL || feof(stdin) || errno != EINVAL ||
            !ferror(stdin) || errno != EINVAL) return 21;
        return 0;
    }
    if (strcmp(mode, "clean-stdin") == 0)
        return !feof(stdin) && !ferror(stdin) ? 0 : 22;
    if (strcmp(mode, "nomem") == 0)
        return fopen("/tmp/stream-input", "r") == NULL && errno == ENOMEM ? 0 : 23;
    if (strcmp(mode, "denied") == 0)
        return fopen("/tmp/stream-input", "r") == NULL && errno == EACCES ? 0 : 24;
    if (strcmp(mode, "emfile") == 0)
        return fopen("/tmp/stream-input", "r") == NULL && errno == EMFILE ? 0 : 25;
    if (strcmp(mode, "close-error") == 0) {
        if (fclose(first) != EOF || errno != EIO) return 26;
        return fclose(first) == EOF && errno == EINVAL ? 0 : 27;
    }
    if (strcmp(mode, "closed-again") == 0)
        return fclose(first) == EOF && errno == EINVAL ? 0 : 28;
    if (strcmp(mode, "read-error") == 0) {
        errno = EPIPE;
        if (getc(first) != EOF || errno != EIO || feof(first) || !ferror(first))
            return 29;
        if (getc(first) != 'R' || errno != EIO || feof(first) || !ferror(first))
            return 30;
        return getc(first) == EOF && feof(first) && ferror(first) &&
               errno == EIO ? 0 : 31;
    }
    if (strcmp(mode, "unavailable") == 0) {
        if (fopen("/tmp/stream-input", "r") != NULL || errno != ENOSYS ||
            fclose(stdin) != EOF || errno != ENOSYS || fclose(first) != EOF ||
            errno != ENOSYS || getc(first) != EOF || errno != ENOSYS ||
            feof(first) || errno != ENOSYS || !ferror(first) || errno != ENOSYS)
            return 32;
        errno = EPIPE;
        return !feof(stdout) && !feof(stderr) && errno == EPIPE ? 0 : 33;
    }
    return 91;
}
