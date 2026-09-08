#include <err.h>
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
    int sync_fd;
    if (argc < 3)
        return 2;
    sync_fd = parse_fd(argv[2]);

    if (argv[1][0] == 'A') {
        unsigned char payload[10000];
        size_t index;
        for (index = 0; index < sizeof(payload); ++index)
            payload[index] = (unsigned char)(index & 0xff);
        if (write(sync_fd, payload, sizeof(payload)) !=
            (ssize_t)sizeof(payload))
            return 3;
    } else {
        unsigned char buffer[777];
        for (;;) {
            ssize_t count = read(sync_fd, buffer, sizeof(buffer));
            if (count < 0)
                return 4;
            if (count == 0)
                break;
        }
    }

    errx(1, "boom");
    return 99;
}
