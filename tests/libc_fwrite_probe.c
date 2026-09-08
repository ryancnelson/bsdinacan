#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    const char *mode;
    if (argc != 2) return 1;
    mode = argv[1];

    /* This path deliberately does not access errno or query flags. */
    if (strcmp(mode, "zero_quiet") == 0)
        return fwrite((void *)1, 0, SIZE_MAX, (FILE *)1) == 0 &&
               fwrite(NULL, SIZE_MAX, 0, NULL) == 0 &&
               fwrite(NULL, 0, 0, stdin) == 0 ? 0 : 40;
    if (strcmp(mode, "split") == 0) {
        errno = ENOENT;
        return fwrite("abcdefgh", 4, 2, stdout) == 2 && errno == ENOENT &&
               !ferror(stdout) ? 0 : 41;
    }
    if (strcmp(mode, "partial_recover") == 0) {
        errno = ENOENT;
        if (fwrite("abcdefgh", 4, 2, stdout) != 1 || errno != EPIPE ||
            !ferror(stdout) || ferror(stderr)) return 42;
        return fwrite("ij", 2, 1, stdout) == 1 && errno == EPIPE &&
               ferror(stdout) && !ferror(stderr) ? 0 : 43;
    }
    if (strcmp(mode, "clip") == 0) {
        /* No-write adapter observes the request; it never reads this buffer. */
        errno = ENOENT;
        return fwrite("x", 1, SIZE_MAX, stdout) == 0 && errno == EIO &&
               ferror(stdout) ? 0 : 44;
    }
    if (strcmp(mode, "wide_return") == 0) {
        errno = ENOENT;
        return fwrite("abcdefgh", 4, 2, stdout) == 0 && errno == EIO &&
               ferror(stdout) ? 0 : 45;
    }
    if (strcmp(mode, "bounds") == 0) {
        errno = ENOENT;
        if (EOVERFLOW != 84 || fwrite("x", SIZE_MAX / 2 + 1, 2, stdout) ||
            errno != EOVERFLOW || ferror(stdout) || ferror(stderr)) return 46;
        if (fwrite(NULL, SIZE_MAX, 2, stdout) || errno != EOVERFLOW ||
            fwrite(NULL, 1, 1, stdout) || errno != EINVAL ||
            fwrite("x", SIZE_MAX, 2, stdin) || errno != EINVAL ||
            fwrite("x", 1, 1, NULL) || errno != EINVAL ||
            fwrite("x", 1, 1, (FILE *)1) || errno != EINVAL) return 47;
        return !ferror(stdout) && !ferror(stderr) ? 0 : 48;
    }

    if (strcmp(mode, "ordinary") == 0) {
        const char binary[] = "\x00\xff" "AB";
        if (fwrite(binary, 1, 4, stdout) != 4) return 1;
        if (fwrite("error", 1, 5, stderr) != 5) return 2;
        if (fwrite("zero", 0, 1, stdout) != 0) return 3;
        if (fwrite("zero", 1, 0, stdout) != 0) return 4;
        return 0;
    }

    if (strcmp(mode, "full") == 0) {
        errno = ENOENT;
        if (fwrite("abcd", 2, 2, stdout) != 2 || errno != ENOENT) return 5;
        if (ferror(stdout)) return 6;
        return 0;
    }

    if (strcmp(mode, "short") == 0) {
        errno = ENOENT;
        if (fwrite("abcdef", 2, 3, stdout) != 3 || errno != ENOENT) return 7;
        if (ferror(stdout)) return 8;
        return 0;
    }

    if (strcmp(mode, "zero_progress") == 0) {
        errno = ENOENT;
        if (fwrite("ab", 1, 2, stdout) != 0 || errno != EIO) return 9;
        if (!ferror(stdout)) return 10;
        return 0;
    }

    if (strcmp(mode, "partial_zero") == 0) {
        errno = ENOENT;
        /* 6 bytes emitted, size is 4 => 1 complete element, remaining 2 ignored */
        if (fwrite("abcdefgh", 4, 2, stdout) != 1 || errno != EIO) return 11;
        if (!ferror(stdout)) return 12;
        return 0;
    }

    if (strcmp(mode, "partial_negative") == 0) {
        errno = ENOENT;
        /* 6 bytes emitted, size is 4 => 1 complete element */
        if (fwrite("abcdefgh", 4, 2, stdout) != 1 || errno != EPIPE) return 13;
        if (!ferror(stdout)) return 14;
        return 0;
    }

    if (strcmp(mode, "oversized") == 0) {
        errno = ENOENT;
        if (fwrite("abcdef", 2, 3, stdout) != 0 || errno != EIO) return 15;
        if (!ferror(stdout)) return 16;
        return 0;
    }

    if (strcmp(mode, "sticky") == 0) {
        /* assumes stdout is already sticky error from partial_negative */
        errno = ENOENT;
        if (fwrite("abcd", 2, 2, stdout) != 2 || errno != ENOENT) return 17;
        if (!ferror(stdout)) return 18; /* sticky error persists */
        return 0;
    }

    if (strcmp(mode, "stderr_write") == 0) {
        if (fwrite("err", 1, 3, stderr) != 3) return 19;
        if (ferror(stderr)) return 20;
        return 0;
    }

    if (strcmp(mode, "stderr_isolate") == 0) {
        if (fwrite("abcdefgh", 4, 2, stderr) != 1 || errno != EPIPE) return 21;
        if (!ferror(stderr) || ferror(stdout)) return 22;
        return 0;
    }

    if (strcmp(mode, "zero_requests") == 0) {
        errno = ENOENT;
        if (fwrite((void*)0x1234, 0, 10, stdout) != 0 || errno != ENOENT) return 23;
        if (fwrite((void*)0x1234, 10, 0, stdout) != 0 || errno != ENOENT) return 24;
        return 0;
    }

    if (strcmp(mode, "invalid_streams") == 0) {
        FILE *bad_stream = (FILE *)&argc;
        errno = ENOENT;
        if (fwrite("a", 1, 1, bad_stream) != 0 || errno != EINVAL) return 25;
        return 0;
    }

    if (strcmp(mode, "null_buffer_zero") == 0) {
        errno = ENOENT;
        void *bad = (void *)((uintptr_t)argv[0] & 0);
        if (fwrite(bad, 0, 10, stdout) != 0 || errno != ENOENT) return 90;
        if (fwrite(bad, 10, 0, stdout) != 0 || errno != ENOENT) return 91;
        return 0;
    }

    if (strcmp(mode, "null_buffer") == 0) {
        errno = ENOENT;
        void *bad = (void *)((uintptr_t)argv[0] & 0);
        if (fwrite(bad, 1, 1, stdout) != 0 || errno != EINVAL) return 27;
        if (ferror(stdout)) return 28; /* argument error does not mark stream */
        return 0;
    }

    if (strcmp(mode, "overflow") == 0) {
        errno = ENOENT;
        if (fwrite("a", SIZE_MAX, 2, stdout) != 0 || errno != EOVERFLOW) return 29;
#if SIZE_MAX > 0xffffffffULL
        if (fwrite("a", (size_t)1ULL<<33, (size_t)1ULL<<33, stdout) != 0 || errno != EOVERFLOW) return 30;
#endif
        return 0;
    }

    if (memcmp(mode, "state_", 6) == 0) {
        errno = ENOENT;
        if (strcmp(mode, "state_recover") == 0) {
            if (fwrite("a", 1, 1, stdout) != 1) return 31;
            return 0;
        }
        if (fwrite("a", 1, 1, stdout) != 0 || errno != ENOSYS) return 32;
        return 0;
    }

    return 99;
}
