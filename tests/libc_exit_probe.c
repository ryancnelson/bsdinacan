#include <stdlib.h>
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

int main(int argc, char *argv[])
{
    void *scratch;
    int fd;
    if (argc != 2)
        return 2;
    fd = parse_fd(argv[1]);
    scratch = malloc(64);
    if (scratch == NULL)
        return 3;
    if (write(fd, "A", 1) != 1)
        return 4;
    exit(7);
    write(fd, "B", 1);
    return 99;
}
