#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = "/tmp/libc-truncate";
    char bytes[8];
    off_t length = 5;
    int fd, reader;
    (void)argc;
    (void)argv;
    fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0 || write(fd, "abcdef", 6) != 6)
        return 1;
    if (truncate(path, 3) != 0 || ftruncate(fd, length) != 0)
        return 2;
    reader = open(path, O_RDONLY);
    if (reader < 0 || read(reader, bytes, sizeof(bytes)) != 5 ||
        bytes[0] != 'a' || bytes[1] != 'b' || bytes[2] != 'c' ||
        bytes[3] != 0 || bytes[4] != 0)
        return 3;
    if (ftruncate(reader, 1) != -1 || errno != EBADF)
        return 4;
    if (truncate(path, (off_t)-1) != -1 || errno != EINVAL)
        return 5;
    if (close(reader) != 0 || close(fd) != 0)
        return 6;
    return 0;
}
