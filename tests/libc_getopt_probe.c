#include <string.h>
#include <unistd.h>

static int parse_fd(const char *text)
{
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        ++text;
    }
    return value;
}

static int run_role_a(int report_fd, int sync_fd, int sub_argc,
                      char *sub_argv[])
{
    unsigned char payload[10000];
    unsigned char result;
    size_t index;
    int ch;

    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == -1 && optind == 1) ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 3;

    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = (unsigned char)(index & 0xff);
    if (write(sync_fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload))
        return 4;

    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == -1 && optind == 1) ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 5;
    close(sync_fd);
    return 0;
}

static int run_role_b(int report_fd, int sync_fd, int sub_argc,
                      char *sub_argv[])
{
    unsigned char buffer[777];
    unsigned char result;
    int ch;

    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'x' && optind == 2) ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 6;

    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == -1 && optind == 3) ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 7;

    for (;;) {
        ssize_t count = read(sync_fd, buffer, sizeof(buffer));
        if (count < 0)
            return 8;
        if (count == 0)
            break;
    }
    close(sync_fd);
    return 0;
}

static int run_role_c(int report_fd, int sync_fd, int sub_argc,
                      char *sub_argv[])
{
    unsigned char payload[10000];
    unsigned char result;
    size_t index;
    int ch;

    /* This test is about the scan cursor, not diagnostics (covered
       separately); keep the transcript quiet. */
    opterr = 0;
    /* sub_argv[1] is a single clustered token, e.g. "-xy": two unknown
       options packed into one argv element. The first call must consume
       only 'x' and leave the scan cursor mid-element. */
    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'x') ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 9;

    for (index = 0; index < sizeof(payload); ++index)
        payload[index] = (unsigned char)(index & 0xff);
    if (write(sync_fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload))
        return 10;

    /* Resumed after role D ran its own unrelated getopt() calls in
       between: the mid-cluster cursor must still point at 'y', not have
       been reset or corrupted by the peer task's activity. */
    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'y') ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 11;
    close(sync_fd);
    return 0;
}

static int run_role_d(int report_fd, int sync_fd, int sub_argc,
                      char *sub_argv[])
{
    unsigned char buffer[777];
    unsigned char result;
    int ch;

    opterr = 0;
    /* Unrelated getopt() activity on this task's own argv, interleaved
       between role C's two calls. If the scan cursor were anything but
       task-local, this would corrupt role C's mid-cluster position. */
    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'a') ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 12;
    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'b') ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 13;

    for (;;) {
        ssize_t count = read(sync_fd, buffer, sizeof(buffer));
        if (count < 0)
            return 14;
        if (count == 0)
            break;
    }
    close(sync_fd);
    return 0;
}

static int run_role_e(int report_fd, int sub_argc, char *sub_argv[])
{
    unsigned char result;
    int ch;

    opterr = 0;
    ch = getopt(sub_argc, sub_argv, "");
    result = (ch == '?' && optopt == 'z') ? '1' : '0';
    if (write(report_fd, &result, 1) != 1)
        return 15;
    return 0;
}

int main(int argc, char *argv[])
{
    char *sub_argv[8];
    int sub_argc;
    int report_fd;
    int sync_fd;
    int index;

    if (argc < 4)
        return 2;
    report_fd = parse_fd(argv[2]);
    sync_fd = parse_fd(argv[3]);
    sub_argv[0] = argv[0];
    sub_argc = 1;
    for (index = 4; index < argc && sub_argc < 8; ++index)
        sub_argv[sub_argc++] = argv[index];

    if (argv[1][0] == 'A')
        return run_role_a(report_fd, sync_fd, sub_argc, sub_argv);
    if (argv[1][0] == 'B')
        return run_role_b(report_fd, sync_fd, sub_argc, sub_argv);
    if (argv[1][0] == 'C')
        return run_role_c(report_fd, sync_fd, sub_argc, sub_argv);
    if (argv[1][0] == 'D')
        return run_role_d(report_fd, sync_fd, sub_argc, sub_argv);
    close(sync_fd);
    return run_role_e(report_fd, sub_argc, sub_argv);
}
