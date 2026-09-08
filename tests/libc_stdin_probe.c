#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static unsigned evaluations;
static FILE *once(void) { ++evaluations; return stdin; }
int main(int argc, char **argv)
{
    const char *mode;
    if (argc != 2) return 90;
    mode = argv[1];
    if (strcmp(mode, "clean") == 0) {
        errno = EPIPE;
        return feof(stdin) == 0 && ferror(stdin) == 0 && errno == EPIPE ? 0 : 1;
    }
    if (strcmp(mode, "binary") == 0) {
        errno = EPIPE;
        evaluations = 0;
        if (getc(once()) != 0 || evaluations != 1 || errno != EPIPE) return 2;
        if (getc(stdin) != 255 || errno != EPIPE) return 3;
        if (getc(stdin) != 'A' || feof(stdin) || ferror(stdin) || errno != EPIPE)
            return 4;
        if (getc(stdin) != EOF || !feof(stdin) || ferror(stdin) || errno != EPIPE)
            return 5;
        return getc(stdin) == EOF && errno == EPIPE ? 0 : 6;
    }
    if (strcmp(mode, "eof") == 0) {
        errno = EPIPE;
        return getc(stdin) == EOF && feof(stdin) && !ferror(stdin) &&
               getc(stdin) == EOF && errno == EPIPE ? 0 : 7;
    }
    if (strcmp(mode, "eof-retained") == 0)
        return feof(stdin) && !ferror(stdin) ? 0 : 8;
    if (strcmp(mode, "badfd") == 0) {
        if (close(0) != 0 || getc(stdin) != EOF || errno != EBADF) return 9;
        return !feof(stdin) && ferror(stdin) && errno == EBADF ? 0 : 10;
    }
    if (strcmp(mode, "error-retained") == 0)
        return !feof(stdin) && ferror(stdin) ? 0 : 11;
    if (strcmp(mode, "rebound") == 0) {
        errno = EPIPE;
        return getc(stdin) == 0 && !feof(stdin) && ferror(stdin) &&
               errno == EPIPE ? 0 : 26;
    }
    if (strcmp(mode, "recover") == 0) {
        errno = EPIPE;
        if (getc(stdin) != EOF || errno != EIO || feof(stdin) || !ferror(stdin))
            return 12;
        if (getc(stdin) != 'R' || errno != EIO || feof(stdin) || !ferror(stdin))
            return 13;
        if (getc(stdin) != EOF || !feof(stdin) || !ferror(stdin) || errno != EIO)
            return 14;
        return getc(stdin) == EOF && errno == EIO ? 0 : 15;
    }
    if (strcmp(mode, "invalid") == 0) {
        FILE *bad = (FILE *)1;
        if (getc(bad) != EOF || errno != EINVAL || getc(stdout) != EOF ||
            errno != EINVAL || getc(NULL) != EOF || errno != EINVAL) return 16;
        if (feof(bad) != 0 || errno != EINVAL || feof(NULL) != 0 ||
            errno != EINVAL || !ferror(bad) || errno != EINVAL) return 17;
        return !feof(stdin) && !ferror(stdin) && !ferror(stdout) &&
               !ferror(stderr) ? 0 : 18;
    }
    if (strcmp(mode, "unavailable") == 0) {
        errno = EPIPE;
        if (getc(stdin) != EOF || errno != ENOSYS) return 19;
        errno = EPIPE;
        if (feof(stdin) != 0 || errno != ENOSYS) return 20;
        errno = EPIPE;
        if (!ferror(stdin) || errno != ENOSYS) return 21;
        errno = EPIPE;
        return !feof(stdout) && !feof(stderr) && errno == EPIPE ? 0 : 22;
    }
    if (strcmp(mode, "out-compat") == 0) {
        errno = EPIPE;
        if (putchar('O') != 'O' || fflush(stdout) != 0 || ferror(stdout) ||
            feof(stdout) || feof(stderr) || errno != EPIPE) return 23;
        return 0;
    }
    if (strcmp(mode, "output-error") == 0) {
        if (close(1) != 0 || putchar('X') != EOF || errno != EBADF) return 24;
        return ferror(stdout) && !feof(stdout) && !feof(stderr) && errno == EBADF
               && !feof(stdin) && !ferror(stdin) ? 0 : 25;
    }
    return 91;
}
