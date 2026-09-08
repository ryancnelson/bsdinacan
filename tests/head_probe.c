#include "cannedbsd/abi.h"
#include <string.h>

struct head_case {
    char *args[8];
    const char *input;
    const char *output;
    const char *error;
    int status;
    int large;
    int pipe_input;
};

static const struct head_case cases[] = {
    {{"head", NULL}, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n", "", 0, 0, 0},
    {{"head", "-3", NULL}, "A\nB\nC\nD\n", "A\nB\nC\n", "", 0, 0, 0},
    {{"head", "-q", "-3", NULL}, "", "", "head: illegal option -- 3\nusage: head [-n lines] [file ...]\n", 1, 0, 0},
    {{"head", "-3", "-q", "A", NULL}, "", "1\n", "", 0, 0, 0},
    {{"head", "-n", "1", "-c", "3", NULL}, "abcde", "abc", "", 0, 0, 0},
    {{"head", "-c", "3", "-n", "1", NULL}, "abcde", "abc", "", 0, 0, 0},
    {{"head", "-n", "1", "A", "B", NULL}, "", "==> A <==\n1\n\n==> B <==\n3\n", "", 0, 0, 0},
    {{"head", "miss", "A", NULL}, "", "==> A <==\n1\n", "head: miss: no such file or directory\n", 1, 0, 0},
    {{"head", "-v", "-q", "A", NULL}, "", "1\n", "", 0, 0, 0},
    {{"head", "-q", "-v", "A", NULL}, "", "==> A <==\n1\n", "", 0, 0, 0},
    {{"head", "-", NULL}, "", "", "head: -: no such file or directory\n", 1, 0, 0},
    {{"head", "-c", "0", NULL}, "", "", "head: illegal byte count -- 0\n", 1, 0, 0},
    {{"head", "-z", NULL}, "", "", "head: illegal option -- z\nusage: head [-n lines] [file ...]\n", 1, 0, 0},
    {{"head", "-c", "65538", NULL}, "", "", "", 0, 1, 0},
    {{"head", "-n", "2", NULL}, "\xff\n\xff", "\xff\n\xff", "", 0, 0, 0},
    {{"head", "-c", "10", NULL}, "ab", "ab", "", 0, 0, 0},
    {{"head", NULL}, "", "", "", 0, 0, 0},
    {{"head", "-n", "1", NULL}, "", "pipe\n", "", 0, 0, 1},
    {{"head", "-n", "bad", NULL}, "", "", "head: illegal line count -- bad\n", 1, 0, 0},
    {{"head", "-c", "9223372036854775808", NULL}, "", "", "head: illegal byte count -- 9223372036854775808\n", 1, 0, 0},
    {{"head", "-n", "-1", NULL}, "", "", "head: illegal line count -- -1\n", 1, 0, 0}
};

static int write_bytes(const struct cb_api_v1 *api, int fd,
                       const char *bytes, size_t count)
{
    while (count != 0) {
        cb_ssize_t n = api->write(fd, bytes, count);
        if (n <= 0 || (uint64_t)n > count) return -1;
        bytes += (size_t)n;
        count -= (size_t)n;
    }
    return 0;
}

static int populate(const struct cb_api_v1 *api, const char *path,
                    const char *bytes, int large)
{
    char chunk[256];
    size_t remaining = large ? 65538 : strlen(bytes);
    int fd = api->open(path, CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    int result = 0;
    if (fd < 0) return -1;
    memset(chunk, 'A', sizeof(chunk));
    while (remaining != 0) {
        size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (write_bytes(api, fd, large ? chunk : bytes, n) < 0) {
            result = -1;
            break;
        }
        if (!large) bytes += n;
        remaining -= n;
    }
    if (api->close(fd) < 0) result = -1;
    return result;
}

static int verify_file(const struct cb_api_v1 *api, const char *path,
                       const char *expected, int large)
{
    struct cb_stat_v1 st;
    char chunk[256];
    size_t remaining = large ? 65538 : strlen(expected);
    int fd, result = 0;
    if (api->stat(path, &st) < 0 || st.size != remaining) return -1;
    fd = api->open(path, CB_O_RDONLY, 0);
    if (fd < 0) return -1;
    while (remaining != 0) {
        size_t i, request = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        cb_ssize_t n = api->read(fd, chunk, request);
        if (n <= 0 || (uint64_t)n > request) { result = -1; break; }
        for (i = 0; i < (size_t)n; ++i) {
            if (chunk[i] != (large ? 'A' : expected[i])) result = -1;
        }
        if (result < 0) break;
        if (!large) expected += (size_t)n;
        remaining -= (size_t)n;
    }
    if (result == 0 && api->read(fd, chunk, sizeof(chunk)) != 0) result = -1;
    if (api->close(fd) < 0) result = -1;
    return result;
}

static void action(struct cb_spawn_action_v1 *a, int type, int from, int to)
{
    memset(a, 0, sizeof(*a));
    a->abi_version = CB_ABI_VERSION_V1;
    a->struct_size = sizeof(*a);
    a->type = type;
    a->from_fd = from;
    a->to_fd = to;
}

static int producer_main(const struct cb_api_v1 *api, int argc,
                          char *const argv[], char *const envp[])
{
    (void)argc; (void)argv; (void)envp;
    return write_bytes(api, 1, "pipe\n", 5) == 0 ? 0 : 1;
}

const struct cb_program_v1 cb_head_pipe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "headpipeproducer", 0,
    64 * 1024, producer_main
};

static int run_one(const struct cb_api_v1 *api, char *const envp[],
                   const struct head_case *test)
{
    struct cb_spawn_action_v1 a[7];
    int in = -1, out = -1, err = -1, writer = -1, result = -1;
    cb_pid_t child = 0, producer = 0;
    int status, producer_status;
    size_t count = 6;
    if (populate(api, "head-input", test->input, test->large) < 0) goto done;
    if (test->pipe_input) {
        int p[2];
        if (api->pipe(p) < 0) goto done;
        in = p[0]; writer = p[1];
    } else {
        in = api->open("head-input", CB_O_RDONLY, 0);
    }
    out = api->open("head-output", CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    err = api->open("head-error", CB_O_WRONLY | CB_O_CREAT | CB_O_TRUNC, 0644);
    if (in < 0 || out < 0 || err < 0) goto done;
    action(&a[0], CB_SPAWN_DUP2, in, 0);
    action(&a[1], CB_SPAWN_DUP2, out, 1);
    action(&a[2], CB_SPAWN_DUP2, err, 2);
    action(&a[3], CB_SPAWN_CLOSE, in, 0);
    action(&a[4], CB_SPAWN_CLOSE, out, 0);
    action(&a[5], CB_SPAWN_CLOSE, err, 0);
    if (writer >= 0) action(&a[count++], CB_SPAWN_CLOSE, writer, 0);
    if (api->spawn("head", test->args, envp, a, count, &child) < 0) goto done;
    if (writer >= 0) {
        char *args[] = {"headpipeproducer", NULL};
        action(&a[0], CB_SPAWN_DUP2, writer, 1);
        action(&a[1], CB_SPAWN_CLOSE, writer, 0);
        action(&a[2], CB_SPAWN_CLOSE, in, 0);
        action(&a[3], CB_SPAWN_CLOSE, out, 0);
        action(&a[4], CB_SPAWN_CLOSE, err, 0);
        if (api->spawn("headpipeproducer", args, envp, a, 5, &producer) < 0) goto done;
    }
    result = 0;
done:
    if (in >= 0 && api->close(in) < 0) result = -1;
    if (out >= 0 && api->close(out) < 0) result = -1;
    if (err >= 0 && api->close(err) < 0) result = -1;
    if (writer >= 0 && api->close(writer) < 0) result = -1;
    if (child > 0 && (api->waitpid(child, &status) != child || status != test->status)) result = -1;
    if (producer > 0 && (api->waitpid(producer, &producer_status) != producer || producer_status != 0)) result = -1;
    if (result == 0 && (verify_file(api, "head-output", test->output, test->large) < 0 ||
                        verify_file(api, "head-error", test->error, 0) < 0)) result = -1;
    return result;
}

static int probe_main(const struct cb_api_v1 *api, int argc,
                       char *const argv[], char *const envp[])
{
    size_t i;
    (void)argc; (void)argv;
    if (api->chdir("/tmp") < 0 || populate(api, "A", "1\n", 0) < 0 ||
        populate(api, "B", "3\n", 0) < 0) return 1;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (run_one(api, envp, &cases[i]) < 0) return (int)i + 20;
    }
    return 0;
}

const struct cb_program_v1 cb_head_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "headprobe", 0,
    64 * 1024, probe_main
};
