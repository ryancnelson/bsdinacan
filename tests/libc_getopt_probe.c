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
    return run_role_b(report_fd, sync_fd, sub_argc, sub_argv);
}
