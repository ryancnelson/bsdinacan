#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void report_error(const char *subject)
{
    static const char prefix[] = "wc: ";
    static const char separator[] = ": ";
    static const char newline[] = "\n";
    int saved_error = errno;
    const char *message = strerror(saved_error);
    size_t subject_length = strlen(subject);
    size_t message_length = strlen(message);
    write_all(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    write_all(STDERR_FILENO, subject, subject_length);
    write_all(STDERR_FILENO, separator, sizeof(separator) - 1);
    write_all(STDERR_FILENO, message, message_length);
    write_all(STDERR_FILENO, newline, sizeof(newline) - 1);
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
    int descriptor = STDIN_FILENO;
    int file_argument = 0;
    const char *subject = "standard input";
    uint64_t count;
    if (argc < 2 || strcmp(argv[1], "-c") != 0 || argc > 3) {
        write_all(STDERR_FILENO, usage, sizeof(usage) - 1);
        return 2;
    }
    if (argc == 3) {
        descriptor = open(argv[2], O_RDONLY);
        if (descriptor < 0) {
            report_error(argv[2]);
            return 1;
        }
        file_argument = 1;
        subject = argv[2];
    }
    if (count_bytes(descriptor, &count) < 0) {
        if (file_argument)
            close(descriptor);
        report_error(subject);
        return 1;
    }
    if (file_argument && close(descriptor) < 0)
        return 1;
    return write_count(count) < 0 ? 1 : 0;
}
