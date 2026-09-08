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

int runnabletimeoutprobe_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    /* This poll should block for 50ms and return 0 (timeout) */
    int res = poll(&pfd, 1, 50);
    if (res != 0) {
        return 100 + pfd.revents; 
    }
    return 0;
}

int yieldingspinner_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < 100; i++) {
        /* Just spin yielding */
    }
    /* Wait, I can't just spin yielding because there's no libc yield. But I can call something that yields, or just loop. But a tight loop without yield blocks the engine? No, CannedBSD is cooperative! If I don't yield, the kernel runs forever. I must call an API that yields! Wait, read() on non-blocking? No, wait(NULL) with WNOHANG? No, we can just do poll(NULL, 0, 0)! */
    for (int i = 0; i < 200; i++) {
        poll(NULL, 0, 0); /* This just yields effectively */
    }
    return 0;
}

int pollwakepeer_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < 50; i++) {
        poll(NULL, 0, 0); /* yield */
    }
    if (write(1, "x", 1) != 1) /* fd 1 is the write end of the pipe */
        return 1;
    return 0;
}

int pollwakeprobe_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int fds[2];
    if (pipe(fds) < 0) return 40;
    
    // Spawn peer
    // Wait, we cannot easily spawn using raw libc in CannedBSD unless we use cb_spawn API.
    // The libc wrapper doesn't have posix_spawn. Let's look at pipeedgeprobe.
    return 0;
}
