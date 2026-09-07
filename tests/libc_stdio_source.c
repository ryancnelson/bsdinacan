#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int count;

    if (argc == 2 && argv[1][0] == 'e') {
        if (close(1) < 0)
            return 10;
        return printf("unwritten:%s", "value") == EOF && errno == EBADF ?
            0 : 11;
    }
    if (argc == 2 && argv[1][0] == 'p')
        return printf("broken") == EOF && errno == EPIPE ? 0 : 16;
    if (argc == 2 && argv[1][0] == 'u')
        return printf("prefix:%d", 1) == EOF && errno == EINVAL ? 0 : 15;

    count = printf("out:%s:%%", "value");
    if (count != 11)
        return 12;
    count = fprintf(stdout, "/%s\n", (char *)0);
    if (count != 8)
        return 13;
    count = fprintf(stderr, "err:%s\n", "bad");
    return count == 8 ? 0 : 14;
}
