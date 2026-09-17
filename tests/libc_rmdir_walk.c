#include <unistd.h>
#include <errno.h>

/* Tree built by the caller (rmdirprobe_main in tests/test_core.c) via the
   raw api layer before invoking this through cb_libc_start, matching the
   FTS-CORE-01 walk-file pattern: rmdir has no self-contained way to make
   its own test directory since mkdir has no public libc wrapper yet. */
int cb_rmdir_walk_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (rmdir("/tmp/rm01_rmdir_file") != -1 || errno != ENOTDIR)
        return 1;
    if (rmdir("/tmp/rm01_rmdir_target") < 0)
        return 2;
    if (rmdir("/tmp/rm01_rmdir_target") != -1 || errno != ENOENT)
        return 3;
    if (rmdir("/tmp/rm01_rmdir_nonexistent") != -1 || errno != ENOENT)
        return 4;
    return 0;
}
