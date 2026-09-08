#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] == 'b') {
        errno = EBADF;
        err(8, "%s", "beta");
    }
    if (argc > 1 && argv[1][0] == 'n') {
        errno = ENOENT;
        err(7, NULL);
    }
    if (argc > 1 && argv[1][0] == 'e') {
        errno = ENOENT;
        err(7, "");
    }
    if (argc > 1 && argv[1][0] == 'c')
        close(STDERR_FILENO);
    errno = ENOENT;
    err(7, "path %s %%", "alpha");
    /* Reaching this write would prove that err returned. */
    write(STDOUT_FILENO, "continued", 9);
    return 99;
}
