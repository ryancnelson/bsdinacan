#define CANNEDBSD_SOURCE_FENCE
#include <stdio.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    errno = 0;
    if (putchar('A') != EOF) return 2;
    if (errno != ENOSYS) return 3;
    
    errno = 0;
    if (fflush(stdout) != EOF) return 4;
    if (errno != ENOSYS) return 5;
    
    errno = 0;
    if (ferror(stdout) == 0) return 6;
    if (errno != ENOSYS) return 7;

    return 0;
}
