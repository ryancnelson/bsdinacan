#include <string.h>

int main(int argc, char **argv)
{
    char dest[16];
    char src[16];
    char *result;

    (void)argc;
    (void)argv;

    /* 1. Empty string */
    dest[0] = 'X'; dest[1] = 'Y';
    result = strcpy(dest, "");
    if (result != dest) return 1;
    if (dest[0] != '\0') return 2;
    if (dest[1] != 'Y') return 3; /* canary */

    /* 2. Normal string including NUL */
    dest[0] = 'X'; dest[1] = 'Y'; dest[2] = 'Z'; dest[3] = 'W';
    result = strcpy(dest, "AB");
    if (result != dest) return 4;
    if (dest[0] != 'A' || dest[1] != 'B' || dest[2] != '\0') return 5;
    if (dest[3] != 'W') return 6; /* canary */

    /* 3. Unsigned byte data / binary bytes */
    dest[0] = 'X'; dest[1] = 'Y'; dest[2] = 'Z'; dest[3] = 'W';
    src[0] = (char)0x80; src[1] = (char)0xff; src[2] = '\0'; src[3] = 'P';
    result = strcpy(dest, src);
    if (result != dest) return 7;
    if (dest[0] != (char)0x80 || dest[1] != (char)0xff || dest[2] != '\0') return 8;
    if (dest[3] != 'W') return 9; /* canary */

    /* 4. Input preservation */
    if (src[0] != (char)0x80 || src[1] != (char)0xff || src[2] != '\0' || src[3] != 'P') return 10;

    return 0;
}
