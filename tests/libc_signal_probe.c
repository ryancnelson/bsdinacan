#include <signal.h>
#include <errno.h>
#include <string.h>

static int handler_called;
static void unsupported(int sig) { (void)sig; ++handler_called; }

/* Ordinary private-header source: no runtime-private headers or state reads. */
int main(int argc, char **argv)
{
    const char *mode;
    if (argc != 2) return 90;
    mode = argv[1];
    errno = EPIPE;
    if (strcmp(mode, "basic") == 0) {
        if (SIG_IGN == SIG_ERR || SIG_IGN == SIG_DFL || SIG_ERR == SIG_DFL)
            return 10;
        if (signal(SIGINT, SIG_DFL) != SIG_DFL || errno != EPIPE) return 11;
        if (signal(SIGINT, SIG_IGN) != SIG_DFL || errno != EPIPE) return 12;
        if (signal(SIGINT, SIG_IGN) != SIG_IGN || errno != EPIPE) return 13;
        if (signal(SIGINT, SIG_DFL) != SIG_IGN || errno != EPIPE) return 14;
        return 0;
    }
    if (strcmp(mode, "unavailable") == 0) {
        if (signal(SIGINT, SIG_IGN) != SIG_ERR || errno != ENOSYS) return 15;
        return 0;
    }
    if (strcmp(mode, "reject") == 0) {
        if (signal(SIGINFO, SIG_IGN) != SIG_ERR || errno != EINVAL) return 16;
        if (signal(-1, SIG_DFL) != SIG_ERR || errno != EINVAL) return 17;
        if (signal(SIGINT, unsupported) != SIG_ERR || errno != ENOSYS) return 18;
        if (signal(SIGINT, SIG_ERR) != SIG_ERR || errno != ENOSYS || handler_called)
            return 19;
        return 0;
    }
    if (strcmp(mode, "ignore") == 0)
        return signal(SIGINT, SIG_IGN) == SIG_DFL && errno == EPIPE ? 0 : 20;
    if (strcmp(mode, "inherited") == 0)
        return signal(SIGINT, SIG_IGN) == SIG_IGN && errno == EPIPE ? 0 : 21;
    if (strcmp(mode, "restore") == 0)
        return signal(SIGINT, SIG_DFL) == SIG_IGN && errno == EPIPE ? 0 : 22;
    if (strcmp(mode, "default") == 0)
        return signal(SIGINT, SIG_DFL) == SIG_DFL && errno == EPIPE ? 0 : 23;
    return 91;
}
