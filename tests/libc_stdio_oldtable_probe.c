#include <stdio.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    errno = 0;
    if (putchar('A') != EOF || errno != ENOSYS) return 2;
    errno = 0;
    if (fflush(stdout) != EOF || errno != ENOSYS) return 3;
    errno = 0;
    if (!ferror(stdout) || errno != ENOSYS) return 4;
    return 0;
}
