#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    int fd;
    (void)argc;
    (void)argv;

    fd = open("/tmp/rm01_unlink_target", O_WRONLY | O_CREAT, 0600);
    if (fd < 0)
        return 1;
    if (close(fd) < 0)
        return 2;

    /* Reopening before unlink must succeed -- proves the file really
       exists before the call under test. */
    fd = open("/tmp/rm01_unlink_target", O_RDONLY);
    if (fd < 0)
        return 3;
    if (close(fd) < 0)
        return 4;

    if (unlink("/tmp/rm01_unlink_target") < 0)
        return 5;

    if (open("/tmp/rm01_unlink_target", O_RDONLY) != -1 || errno != ENOENT)
        return 6;

    if (unlink("/tmp/rm01_unlink_target") != -1 || errno != ENOENT)
        return 7;

    return 0;
}
