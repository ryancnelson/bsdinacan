#include <errno.h>
#include <string.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    if (ENOEXEC != 8)
        return 1;
    errno = ENOEXEC;
    if (strcmp(strerror(ENOEXEC), "exec format error") != 0 || errno != ENOEXEC)
        return 2;
    return 0;
}
