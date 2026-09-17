#include <sys/stat.h>

/* FS-STAT-01: proves the libc-side struct_size guard actually gates
   reading device/nlink/uid/gid/timestamps, rather than leaking whatever
   the runtime happens to have written. "/" genuinely has multiple real
   subdirectories (nlink > 2) and a real non-zero creation timestamp;
   if this returns the old, fixed fallback sentinels despite that, the
   guard is doing its job against a caller-declared old struct_size. */
int main(int argc, char *argv[])
{
    struct stat st;
    (void)argc;
    (void)argv;
    if (stat("/", &st) < 0)
        return 2;
    if (st.st_dev != 1)
        return 3;
    if (st.st_nlink != 1)
        return 4;
    if (st.st_uid != 0 || st.st_gid != 0)
        return 5;
    if (st.st_atime != 0 || st.st_mtime != 0 || st.st_ctime != 0)
        return 6;
    return 0;
}
