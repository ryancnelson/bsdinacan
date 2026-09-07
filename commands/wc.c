#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static int strings_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int write_all(int descriptor, const void *buffer, size_t count)
{
    const unsigned char *cursor = buffer;
    while (count != 0) {
        ssize_t result = write(descriptor, cursor, count);
        if (result <= 0)
            return -1;
        cursor += (size_t)result;
        count -= (size_t)result;
    }
    return 0;
}

static int write_count(uint64_t count)
{
    char buffer[32];
    size_t position = sizeof(buffer);
    buffer[--position] = '\n';
    do {
        buffer[--position] = (char)('0' + count % 10);
        count /= 10;
    } while (count != 0);
    return write_all(STDOUT_FILENO, buffer + position,
                     sizeof(buffer) - position);
}

static int count_bytes(int descriptor, uint64_t *count_out)
{
    unsigned char *buffer = malloc(1024);
    uint64_t total = 0;
    if (buffer == NULL)
        return -1;
    for (;;) {
        ssize_t count = read(descriptor, buffer, 1024);
        if (count < 0) {
            free(buffer);
            return -1;
        }
        if (count == 0) {
            free(buffer);
            *count_out = total;
            return 0;
        }
        if ((uint64_t)count > UINT64_MAX - total) {
            free(buffer);
            return -1;
        }
        total += (uint64_t)count;
    }
}

int main(int argc, char *argv[])
{
    static const char usage[] = "usage: wc -c [file]\n";
    static const char read_error[] = "wc: input error\n";
    int descriptor = STDIN_FILENO;
    int file_argument = 0;
    uint64_t count;
    if (argc < 2 || !strings_equal(argv[1], "-c") || argc > 3) {
        write_all(STDERR_FILENO, usage, sizeof(usage) - 1);
        return 2;
    }
    if (argc == 3) {
        descriptor = open(argv[2], O_RDONLY);
        if (descriptor < 0) {
            write_all(STDERR_FILENO, read_error, sizeof(read_error) - 1);
            return 1;
        }
        file_argument = 1;
    }
    if (count_bytes(descriptor, &count) < 0) {
        if (file_argument)
            close(descriptor);
        write_all(STDERR_FILENO, read_error, sizeof(read_error) - 1);
        return 1;
    }
    if (file_argument && close(descriptor) < 0)
        return 1;
    return write_count(count) < 0 ? 1 : 0;
}
