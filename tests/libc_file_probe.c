#include <stdio.h>
#include <sys/stat.h>
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
    if (strcmp(mode, "default-mode") == 0) {
        int fd = open("/tmp/default-mode", O_CREAT | O_WRONLY, DEFFILEMODE);
        if (fd != 3) { if (fd >= 0) close(fd); return 34; }
        return close(fd) == 0 ? 0 : 35;
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
