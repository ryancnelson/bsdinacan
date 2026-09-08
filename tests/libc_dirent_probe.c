#include <dirent.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    DIR *dirp;
    struct dirent *entry;
    int seen_tmp;
    (void)argc;
    (void)argv;

    dirp = opendir("/");
    if (dirp == NULL)
        return 2;
    seen_tmp = 0;
    for (;;) {
        errno = 0;
        entry = readdir(dirp);
        if (entry == NULL) {
            if (errno != 0) {
                closedir(dirp);
                return 4;
            }
            break;
        }
        if (entry->d_name[0] == 't' && entry->d_name[1] == 'm' &&
            entry->d_name[2] == 'p' && entry->d_name[3] == '\0')
            seen_tmp = 1;
    }
    if (closedir(dirp) < 0)
        return 3;
    return seen_tmp ? 0 : 1;
}
