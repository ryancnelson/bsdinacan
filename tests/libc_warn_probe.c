#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    /* Rewrite argv[0] to test stable identity preservation. */
    if (argc > 0)
        argv[0] = "/usr/bin/rewritten";

    if (argc > 1 && argv[1][0] == 'o') {
        /* ordinary format */
        errno = EBADF;
        warn("ordinary %s", "format");
        if (errno != EBADF) return 1;
    } else if (argc > 1 && argv[1][0] == 'n') {
        /* null format */
        errno = ENOENT;
        warn(NULL);
        if (errno != ENOENT) return 1;
    } else if (argc > 1 && argv[1][0] == 'e') {
        /* empty format */
        errno = ENOENT;
        warn("");
        if (errno != ENOENT) return 1;
    } else if (argc > 1 && argv[1][0] == 'f') {
        /* failed stderr preserves errno */
        if (close(STDERR_FILENO) != 0) return 2;
        errno = ENOEXEC;
        warn("failed stderr");
        if (errno == ENOEXEC) {
            if (write(STDOUT_FILENO, "preserved\n", 10) != 10) return 3;
        } else {
            if (write(STDOUT_FILENO, "corrupted\n", 10) != 10) return 3;
            return 1;
        }
        return 0;
    }

    if (write(STDOUT_FILENO, "continued\n", 10) != 10) return 3;
    return 0;
}
