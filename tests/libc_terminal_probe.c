#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct termios attributes, before;
    int fd, pipes[2], index;
    (void)argc;
    (void)argv;
    /* A failed query must leave the caller's storage untouched. */
    for (index = 0; index < (int)sizeof(attributes); ++index)
        ((unsigned char *)&attributes)[index] = 0xa5;
    before = attributes;
    for (fd = 0; fd < 3; ++fd) {
        errno = EIO;
        if (isatty(fd) != 1 || errno != EIO) return 1;
        if (tcgetattr(fd, &attributes) != -1 || errno != ENOSYS ||
            memcmp(&attributes, &before, sizeof(attributes)) != 0) return 2;
        if (tcgetattr(fd, NULL) != -1 || errno != EINVAL) return 3;
        if (tcsetattr(fd, TCSANOW, NULL) != -1 || errno != EINVAL) return 4;
        for (index = -1; index <= TCSAFLUSH + 1; ++index) {
            if (tcsetattr(fd, index, &attributes) != -1 || errno != ENOSYS ||
                memcmp(&attributes, &before, sizeof(attributes)) != 0) return 5;
        }
    }
    for (index = 0; index < (int)sizeof(attributes); ++index)
        ((unsigned char *)&attributes)[index] = 0;
    if (tcsetattr(0, TCSANOW, &attributes) != -1 || errno != ENOSYS) return 11;
    fd = open("/tmp/terminal-regular", O_CREAT | O_RDWR, 0600);
    if (fd < 0) return 6;
    if (isatty(fd) != 0 || errno != ENOTTY ||
        tcgetattr(fd, &attributes) != -1 || errno != ENOTTY ||
        tcsetattr(fd, TCSANOW, &attributes) != -1 || errno != ENOTTY ||
        tcgetattr(fd, NULL) != -1 || errno != ENOTTY) return 7;
    if (close(fd) != 0 || pipe(pipes) != 0) return 8;
    for (index = 0; index < 2; ++index) {
        if (isatty(pipes[index]) != 0 || errno != ENOTTY ||
            tcgetattr(pipes[index], &attributes) != -1 || errno != ENOTTY ||
            tcsetattr(pipes[index], TCSANOW, &attributes) != -1 ||
            errno != ENOTTY || close(pipes[index]) != 0) return 9;
    }
    /* The closed descriptor also proves classification follows lifetime. */
    if (isatty(pipes[0]) != 0 || errno != EBADF ||
        isatty(-1) != 0 || errno != EBADF ||
        isatty(999) != 0 || errno != EBADF ||
        tcgetattr(-1, NULL) != -1 || errno != EBADF ||
        tcsetattr(999, TCSANOW, &attributes) != -1 || errno != EBADF)
        return 10;
    return 0;
}

/* Each character states independently whether a particular API field exists.
 * Invalid attribute descriptors distinguish a real callback from fallback. */
int terminal_optional_main(int argc, char **argv)
{
    struct termios attributes;
    struct pollfd descriptor = {1, POLLOUT, 0};
    int available;
    if (argc != 2) return 20;
    available = argv[1][0] == '1';
    errno = EIO;
    if (isatty(0) != available || errno != (available ? EIO : ENOSYS))
        return 21;
    if (tcgetattr(-1, &attributes) != -1 ||
        errno != (argv[1][1] == '1' ? EBADF : ENOSYS)) return 22;
    if (tcsetattr(-1, TCSANOW, &attributes) != -1 ||
        errno != (argv[1][2] == '1' ? EBADF : ENOSYS)) return 23;
    if (argv[1][3] == '1') {
        if (poll(&descriptor, 1, 0) != 1 || descriptor.revents != POLLOUT)
            return 24;
    } else if (poll(&descriptor, 1, 0) != -1 || errno != ENOSYS) return 25;
    return 0;
}
