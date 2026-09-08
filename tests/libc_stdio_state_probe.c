#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    FILE *bad = (FILE *)(void *)&argc;
    const char *mode;
    if (argc != 2) return 1;
    mode = argv[1];
    if (strcmp(mode, "putchar_ok") == 0) {
        errno = ENOENT;
        if (putchar('A') != 'A' || errno != ENOENT) return 2;
        if (fflush(stdout) != 0 || ferror(stdout) || errno != ENOENT) return 3;
        return 0;
    }
    if (strcmp(mode, "byte") == 0)
        return putchar(-1) == 255 ? 0 : 4;
    if (strcmp(mode, "fflush_invalid") == 0) {
        if (fflush(bad) != EOF || errno != EINVAL) return 5;
        if (!ferror(bad) || errno != EINVAL) return 6;
        if (!ferror(NULL) || errno != EINVAL) return 7;
        errno = ENOENT;
        if (fflush(NULL) || fflush(stdout) || fflush(stderr) ||
            ferror(stdout) || ferror(stderr) || errno != ENOENT) return 8;
        return 0;
    }
    if (strcmp(mode, "write_retry") == 0) {
        errno = ENOENT;
        if (puts("hello") == EOF || ferror(stdout) || errno != ENOENT) return 9;
        return 0;
    }
    if (strcmp(mode, "sticky_error") == 0) {
        if (puts("abcdef") != EOF || errno != EPIPE) return 10;
        if (!ferror(stdout) || ferror(stderr) || errno != EPIPE) return 11;
        if (printf("%s", "OK") != 2 || errno != EPIPE) return 12;
        if (fflush(NULL) != 0 || !ferror(stdout) || errno != EPIPE) return 13;
        return 0;
    }
    if (strcmp(mode, "zero") == 0) {
        if (putchar('A') != EOF || errno != EIO) return 14;
        return ferror(stdout) && !ferror(stderr) && errno == EIO ? 0 : 15;
    }
    if (strcmp(mode, "printf_fail") == 0) {
        if (printf("%s", "failure") >= 0 || errno != EPIPE) return 16;
        return ferror(stdout) && !ferror(stderr) && errno == EPIPE ? 0 : 17;
    }
    if (strcmp(mode, "fprintf_fail") == 0) {
        if (fprintf(stderr, "%s", "failure") >= 0 || errno != EPIPE) return 18;
        return !ferror(stdout) && ferror(stderr) && errno == EPIPE ? 0 : 19;
    }
    if (strcmp(mode, "raw_fail") == 0) {
        if (write(1, "x", 1) != -1 || errno != EPIPE) return 20;
        return !ferror(stdout) && !ferror(stderr) ? 0 : 21;
    }
    if (strcmp(mode, "format") == 0) {
        if (printf("%d", 1) >= 0 || errno != EINVAL) return 22;
        return !ferror(stdout) && !ferror(stderr) ? 0 : 23;
    }
    if (strcmp(mode, "clean") == 0)
        return !ferror(stdout) && !ferror(stderr) ? 0 : 24;
    if (strcmp(mode, "rebind") == 0) {
        if (close(1) != 0 || puts("fail") != EOF || errno != EBADF) return 25;
        return ferror(stdout) && !ferror(stderr) ? 0 : 26;
    }
    if (strcmp(mode, "rebind_persist") == 0)
        return ferror(stdout) && !ferror(stderr) ? 0 : 27;
    if (strcmp(mode, "stderr_dirty") == 0) {
        if (close(2) != 0 || fprintf(stderr, "%s", "fail") >= 0 || errno != EBADF) return 28;
        return !ferror(stdout) && ferror(stderr) ? 0 : 29;
    }
    if (strcmp(mode, "stderr_persist") == 0)
        return !ferror(stdout) && ferror(stderr) ? 0 : 30;
    return 99;
}
