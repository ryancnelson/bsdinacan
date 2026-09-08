/* Exercise the actual Linux backend loop with deterministic syscall results. */
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ssize_t planned_write(int fd, const void *data, size_t count);
#define write planned_write
#include "../src/host_linux.c"
#undef write

struct write_step { ssize_t result; int error; };
struct write_case {
    const char *name;
    struct write_step steps[4];
    size_t step_count;
    cb_ssize_t expected;
    const char *bytes;
    size_t count;
    int stream;
};
static const struct write_case cases[] = {
    {"zero", {{0, 0}}, 1, -CB_EIO, "", 6, 1},
    {"partial-error", {{2, 0}, {-1, EPIPE}}, 2, 2, "ab", 6, 2},
    {"partial-zero", {{2, 0}, {0, 0}}, 2, 2, "ab", 6, 1},
    {"partial-success", {{2, 0}, {1, 0}, {3, 0}}, 3, 6, "abcdef", 6, 2},
    {"error", {{-1, EPIPE}}, 1, -CB_EIO, "", 6, 1},
    {"interrupted", {{-1, EINTR}, {6, 0}}, 2, 6, "abcdef", 6, 1},
    {"partial-interrupted", {{2, 0}, {-1, EINTR}, {4, 0}}, 3, 6, "abcdef", 6, 1},
    {"empty", {{0, 0}}, 0, 0, "", 0, 2}
};
static const struct write_case *current_case;
static const unsigned char payload[] = "abcdef";
static unsigned char emitted[sizeof(payload)];
static size_t calls, emitted_size;

static void fail_case(const char *reason)
{
    fprintf(stderr, "FAIL Linux console write %s: %s\n", current_case->name, reason);
    exit(1);
}

static ssize_t planned_write(int fd, const void *data, size_t count)
{
    struct write_step step;
    if (calls >= current_case->step_count)
        fail_case("unexpected callback after bounded plan (no progress)");
    if (fd != (current_case->stream == 2 ? STDERR_FILENO : STDOUT_FILENO) ||
        data != payload + emitted_size || count != current_case->count - emitted_size)
        fail_case("descriptor, pointer offset or remaining count");
    step = current_case->steps[calls++];
    if (step.result > 0) {
        if ((size_t)step.result > count) fail_case("invalid test plan");
        memcpy(emitted + emitted_size, data, (size_t)step.result);
        emitted_size += (size_t)step.result;
    }
    errno = step.error;
    return step.result;
}

int main(int argc, char **argv)
{
    size_t index, ran = 0;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        cb_ssize_t result;
        current_case = &cases[index];
        if (argc == 2 && strcmp(argv[1], current_case->name) != 0) continue;
        calls = emitted_size = 0;
        memset(emitted, 0xa5, sizeof(emitted));
        result = cb_linux_host_ops()->console_write(current_case->stream,
                   current_case->count ? payload : NULL, current_case->count);
        if (result != current_case->expected) fail_case("returned count/error");
        if (calls != current_case->step_count) fail_case("callback count");
        if (emitted_size != strlen(current_case->bytes) ||
            memcmp(emitted, current_case->bytes, emitted_size) != 0)
            fail_case("emitted bytes");
        ++ran;
    }
    if (!ran) return 2;
    puts("Linux console write cases passed");
    return 0;
}
