#include <stddef.h>
#include <string.h>

int main(int argc, char **argv)
{
    char text[] = "abca", backward[] = "abcdef", forward[] = "abcdef";
    unsigned char high[] = {0x80, 0xff}, low[] = {0x7f, 0xff};
    unsigned char copy[] = {0xa5, 0xa5, 0xa5, 0xa5};
    (void)argc;
    (void)argv;
    if (strchr(text, 'a') != text || strchr(text, 'c') != text + 2 ||
        strchr(text, '\0') != text + 4 || strchr(text, 'z') != NULL ||
        strchr(text, 0x161) != text)
        return 1;
    if (memmove(backward + 2, backward, 4) != backward + 2 ||
        memcmp(backward, "ababcd", 6) != 0 ||
        memmove(forward, forward + 2, 4) != forward ||
        memcmp(forward, "cdefef", 6) != 0)
        return 2;
    if (memcmp(high, low, 2) <= 0 || memcmp(low, high, 2) >= 0 ||
        memcmp(high, low, 0) != 0 || memcmp(high, high, 2) != 0)
        return 3;
    if (memcpy(copy + 1, high, 2) != copy + 1 ||
        copy[0] != 0xa5 || copy[1] != 0x80 || copy[2] != 0xff ||
        copy[3] != 0xa5 || memcpy(copy, low, 0) != copy || copy[0] != 0xa5)
        return 4;
    return 0;
}
