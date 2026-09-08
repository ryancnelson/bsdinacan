#include <poll.h>
#include <unistd.h>
#include <errno.h>

int oldpollprobe_main(int argc, char **argv)
{
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    (void)argc;
    (void)argv;
    int result = poll(&pfd, 1, 0);
    if (result == -1 && errno == ENOSYS)
        return 0; // successfully returned ENOSYS
    return 1; // failed
}

int normalpollprobe_main(int argc, char **argv)
{
    int fds[2];
    struct pollfd pfds[2];
    (void)argc;
    (void)argv;

    if (pipe(fds) < 0)
        return 10;

    pfds[0].fd = fds[0];
    pfds[0].events = POLLIN;
    pfds[1].fd = fds[1];
    pfds[1].events = POLLOUT;

    /* Write space available immediately */
    if (poll(pfds, 2, 0) != 1)
        return 11;
    if (pfds[0].revents != 0)
        return 12;
    if (!(pfds[1].revents & POLLOUT))
        return 13;

    /* Write data */
    if (write(fds[1], "a", 1) != 1)
        return 14;

    /* Now both read and write should be ready */
    if (poll(pfds, 2, 0) != 2)
        return 15;
    if (!(pfds[0].revents & POLLIN))
        return 16;

    /* Invalid fd */
    pfds[0].fd = 999;
    pfds[0].events = POLLIN;
    if (poll(pfds, 1, 0) != 1)
        return 17;
    if (pfds[0].revents != POLLNVAL)
        return 18;

    /* Close write end, read should get HUP */
    close(fds[1]);
    pfds[0].fd = fds[0];
    pfds[0].events = POLLIN;
    if (poll(pfds, 1, 0) != 1)
        return 19;
    if (!(pfds[0].revents & POLLHUP))
        return 20;

        /* Test cb_libc_pipe error translation */
    if (pipe(NULL) != -1 || errno != EINVAL)
        return 23;

    /* Test EMFILE by opening pipes until failure */
    int pipes[200][2];
    int count = 0;
    while (count < 200 && pipe(pipes[count]) == 0) {
        count++;
    }
    if (errno != EMFILE)
        return 24;

    /* Clean up so the rest of the test can run */
    for (int i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (poll(NULL, 1, 0) != -1 || errno != EFAULT)
        return 21;
    if (poll(pfds, -1, 0) != -1 || errno != EINVAL)
        return 22;

    close(fds[0]);
    return 0; // all passed
}

int clocklossprobe_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    /* This poll should block and then observe clock loss, returning ENOSYS */
    if (poll(&pfd, 1, 1000) != -1 || errno != ENOSYS)
        return 30;
    return 0;
}
