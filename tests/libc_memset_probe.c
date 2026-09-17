#include <string.h>

int main(int argc, char *argv[])
{
    unsigned char buffer[64];
    size_t i;
    (void)argc;
    (void)argv;

    /* zero-length: must not touch memory, must return dest */
    buffer[0] = 0x42;
    if (memset(buffer, 0, 0) != buffer || buffer[0] != 0x42)
        return 1;

    /* small fill, below the word-copy threshold */
    memset(buffer, 0xAA, 5);
    for (i = 0; i < 5; ++i)
        if (buffer[i] != 0xAA)
            return 2;

    /* larger fill, unaligned start, crosses the word-copy path */
    memset(buffer + 1, 0x55, 40);
    for (i = 1; i < 41; ++i)
        if (buffer[i] != 0x55)
            return 3;
    if (buffer[0] != 0xAA)
        return 4;

    if (memset(buffer, 0, sizeof(buffer)) != buffer)
        return 5;
    for (i = 0; i < sizeof(buffer); ++i)
        if (buffer[i] != 0)
            return 6;

    return 0;
}
