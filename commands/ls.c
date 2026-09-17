#include <dirent.h>
#include <errno.h>
#include <err.h>
#include <stdio.h>

/*
 * cannedBSD-owned single-column ls: no -l, no multi-column width
 * computation, no termcap, no getpwuid/getgrgid, no fts. See
 * notes/iterations/LS-01.md. Enumerates one directory operand (default ".")
 * over the existing VFS-03 dirent contract and prints one name per line, in
 * whatever order opendir/readdir already yields -- that order is the
 * directory's own stable, deterministic child order, not something this
 * command re-sorts. A named operand that exists but is not a directory is
 * printed by name, matching ls's behavior for a plain file argument; this
 * command does not call stat() (no public struct stat exists yet -- see
 * FILEUTIL-01) and instead distinguishes the two cases using opendir's own
 * ENOTDIR vs. other-error result, which the VFS-03 dirent contract already
 * provides.
 */
int cb_ls_main(int argc, char *argv[]);

int cb_ls_main(int argc, char *argv[])
{
    const char *path;
    DIR *dir;
    struct dirent *entry;
    int status;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [file]\n", argv[0]);
        return 1;
    }
    path = argc == 2 ? argv[1] : ".";

    dir = opendir(path);
    if (dir == NULL) {
        if (errno == ENOTDIR) {
            /* Not a directory: a plain file operand. Print its name and
               stop -- no attempt to open or read the file itself. */
            return puts(path) == EOF ? 1 : 0;
        }
        warn("%s", path);
        return 1;
    }

    status = 0;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL)
            break;
        if (puts(entry->d_name) == EOF) {
            status = 1;
            break;
        }
    }
    if (status == 0 && errno != 0) {
        warn("%s", path);
        status = 1;
    }

    if (closedir(dir) < 0) {
        warn("%s", path);
        status = 1;
    }

    return status;
}
