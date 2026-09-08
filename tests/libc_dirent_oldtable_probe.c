#include <dirent.h>
#include <errno.h>

/*
 * Run through cb_libc_start with a deliberately truncated cb_api_v1 (see
 * test_dirent_libc_old_table_contract in tests/test_core.c): opendir must
 * report ENOSYS, not crash or silently succeed, when the runtime's API
 * struct predates the directory-iteration tail.
 */
int main(int argc, char *argv[])
{
    DIR *dirp;
    (void)argc;
    (void)argv;

    errno = 0;
    dirp = opendir("/");
    if (dirp != NULL)
        return 2;
    if (errno != ENOSYS)
        return 3;
    return 0;
}
