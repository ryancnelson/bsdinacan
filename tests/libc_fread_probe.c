#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static FILE *held;
static void reset_buffer(unsigned char *buffer)
{
    memcpy(buffer, "ZZZZZZZZZZZZZZ", 14);
}
static const unsigned char data[] = {0,255,'A','B','C','D','E','F','G','H','I','J'};
static int bytes(const unsigned char *buffer, size_t count, size_t offset)
{
    size_t i;
    if (buffer[0] != 0x5a) return 0;
    for (i = 0; i < count; ++i)
        if (buffer[i + 1] != data[offset + i]) return 0;
    for (i = count + 1; i < 14; ++i)
        if (buffer[i] != 0x5a) return 0;
    return 1;
}
int main(int argc, char **argv)
{
    unsigned char buffer[14];
    const char *mode;
    FILE *stream;
    int before_eof, before_error;
    if (argc != 2) return 90;
    mode = argv[1];
    /* This branch deliberately makes no errno/status callback of its own. */
    if (strcmp(mode, "zero-quiet") == 0)
        return fread((void *)1, 0, SIZE_MAX, (FILE *)1) == 0 &&
               fread(NULL, SIZE_MAX, 0, NULL) == 0 &&
               fread(NULL, 0, 0, stdout) == 0 ? 0 : 1;
    reset_buffer(buffer);
    errno = EPIPE;
    if (strcmp(mode, "zero") == 0) {
        if (fread((void *)1, 0, SIZE_MAX, (FILE *)1) ||
            fread(NULL, SIZE_MAX, 0, NULL) || errno != EPIPE) return 2;
        return !feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 3;
    }
    if (strcmp(mode, "arguments") == 0) {
        before_eof = feof(stdin); before_error = ferror(stdin);
        if (EOVERFLOW != 84 || strcmp(strerror(EOVERFLOW),
            "value too large to be stored in data type") != 0 || errno != EPIPE) return 4;
        if (fread(buffer + 1, SIZE_MAX / 2 + 1, 2, stdin) || errno != EOVERFLOW)
            return 5;
        if (fread(NULL, SIZE_MAX / 2 + 1, 2, stdin) || errno != EOVERFLOW) return 6;
        if (fread(NULL, 1, 1, stdin) || errno != EINVAL) return 7;
        if (fread(buffer + 1, SIZE_MAX, 2, stdout) || errno != EINVAL ||
            fread(buffer + 1, 1, 1, stderr) || errno != EINVAL ||
            fread(buffer + 1, 1, 1, NULL) || errno != EINVAL ||
            fread(buffer + 1, 1, 1, (FILE *)1) || errno != EINVAL) return 8;
        return feof(stdin) == before_eof && ferror(stdin) == before_error &&
               bytes(buffer, 0, 0) ? 0 : 9;
    }
    if (strcmp(mode, "open-held") == 0) {
        held = fopen("/tmp/fread-input", "rb");
        return held != NULL && errno == EPIPE ? 0 : 27;
    }
    if (strcmp(mode, "held-unavailable") == 0)
        return fread(buffer + 1, 1, 1, held) == 0 && errno == ENOSYS &&
               bytes(buffer, 0, 0) ? 0 : 28;
    if (strcmp(mode, "read-held") == 0) {
        if (fread(buffer + 1, 1, 2, held) != 2 || !bytes(buffer, 2, 0) ||
            feof(held) || ferror(held) || errno != EPIPE) return 29;
        return fclose(held) == 0 && errno == EPIPE ? 0 : 30;
    }
    if (strcmp(mode, "legacy") == 0) {
        if (fread(buffer + 1, 1, 1, stdin) != 1 || !bytes(buffer, 1, 0) ||
            feof(stdin) || ferror(stdin) || errno != EPIPE) return 31;
        return fread(buffer + 1, 1, 1, stdin) == 0 && feof(stdin) &&
               !ferror(stdin) && errno == EPIPE ? 0 : 32;
    }
    if (strcmp(mode, "unavailable") == 0) {
        if (fread(buffer + 1, 1, 1, stdin) || errno != ENOSYS ||
            fread(buffer + 1, SIZE_MAX, 2, stdin) || errno != ENOSYS ||
            fread(buffer + 1, 1, 1, (FILE *)1) || errno != ENOSYS) return 10;
        return bytes(buffer, 0, 0) ? 0 : 11;
    }
    if (strcmp(mode, "full") == 0) {
        if (fread(buffer + 1, 4, 2, stdin) != 2 || !bytes(buffer, 8, 0) ||
            feof(stdin) || ferror(stdin) || errno != EPIPE) return 12;
        if (fread(buffer + 1, 1, 1, stdin) || !feof(stdin) || ferror(stdin) ||
            errno != EPIPE || fread(buffer + 1, 1, 1, stdin)) return 13;
        return 0;
    }
    if (strcmp(mode, "bytes") == 0)
        return fread(buffer + 1, 1, 12, stdin) == 12 && bytes(buffer, 12, 0) &&
               !feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 14;
    if (strcmp(mode, "partial-eof") == 0)
        return fread(buffer + 1, 4, 2, stdin) == 1 && bytes(buffer, 6, 0) &&
               feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 15;
    if (strcmp(mode, "small-eof") == 0)
        return fread(buffer + 1, 4, 1, stdin) == 0 && bytes(buffer, 2, 0) &&
               feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 16;
    if (strcmp(mode, "partial-error") == 0) {
        if (fread(buffer + 1, 4, 2, stdin) != 1 || !bytes(buffer, 6, 0) ||
            feof(stdin) || !ferror(stdin) || errno != EIO) return 17;
        reset_buffer(buffer);
        /* Previous partial element was consumed, not buffered for this call. */
        return fread(buffer + 1, 4, 1, stdin) == 1 && bytes(buffer, 4, 6) &&
               !feof(stdin) && ferror(stdin) && errno == EIO ? 0 : 18;
    }
    if (strcmp(mode, "error-first") == 0 || strcmp(mode, "overreturn") == 0)
        return fread(buffer + 1, 4, 2, stdin) == 0 && bytes(buffer, 0, 0) &&
               !feof(stdin) && ferror(stdin) && errno == EIO ? 0 : 19;
    if (strcmp(mode, "eof-first") == 0)
        return fread(buffer + 1, 4, 2, stdin) == 0 && bytes(buffer, 0, 0) &&
               feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 20;
    if (strcmp(mode, "clip") == 0) {
        /* A no-write mock inspects this limit; no huge buffer is dereferenced. */
        return fread(buffer + 1, 1, SIZE_MAX, stdin) == 0 && bytes(buffer, 0, 0) &&
               feof(stdin) && !ferror(stdin) && errno == EPIPE ? 0 : 21;
    }
    if (strcmp(mode, "dynamic") == 0) {
        stream = fopen("/tmp/fread-input", "rb");
        if (stream == NULL || fread(buffer + 1, 4, 2, stream) != 2 ||
            !bytes(buffer, 8, 0) || feof(stream) || ferror(stream) || errno != EPIPE)
            return 22;
        if (getc(stream) != 'G') return 23;
        reset_buffer(buffer);
        if (fread(buffer + 1, 4, 1, stream) != 0 || !bytes(buffer, 3, 9) ||
            !feof(stream) || ferror(stream) || errno != EPIPE) return 24;
        return fclose(stream) == 0 && errno == EPIPE ? 0 : 25;
    }
    if (strcmp(mode, "closed") == 0) {
        if (fclose(stdin) != 0 || fread(buffer + 1, 1, 1, stdin) != 0 ||
            errno != EINVAL) return 26;
        return 0;
    }
    return 91;
}
