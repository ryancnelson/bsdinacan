#include <locale.h>
#include <errno.h>
#include <string.h>
#include "libc_strcasecmp_cases.h"

static int is_c(const char *name)
{
    return name != NULL && strcmp(name, "C") == 0;
}

static int collate_checks(void)
{
    static const char high[] = {(char)0x80, 0};
    static const char highest[] = {(char)0xff, 0};
    static const char low[] = {(char)0x7f, 0};
    errno = EIO;
    if (strcoll("", "") != 0 || strcoll("same", "same") != 0 ||
        strcoll("", "a") >= 0 || strcoll("a", "") <= 0 ||
        strcoll("a", "aa") >= 0 || strcoll("aa", "a") <= 0 ||
        strcoll("Z", "a") >= 0 || strcoll(high, low) <= 0 ||
        strcoll(highest, high) <= 0 || errno != EIO) return 40;
    if (setlocale(LC_ALL, "unsupported") != NULL ||
        strcoll("Z", "a") >= 0 || errno != EIO) return 41;
    if (!is_c(setlocale(LC_ALL, "POSIX")) ||
        strcoll("prefix", "prefix-more") >= 0 || errno != EIO) return 42;
    return 0;
}

int main(int argc, char **argv)
{
    const char *held;
    const char *unsupported[] = {"C.UTF-8", "en_US.UTF-8", "c", "POSIX.UTF-8",
                                 "LC_CTYPE=C;LC_NUMERIC=C"};
    size_t index;
    if (argc == 2 && strcmp(argv[1], "collate") == 0) return collate_checks();
    if (argc == 2 && strcmp(argv[1], "casecmp") == 0) return casecmp_checks();
    errno = EIO;
    held = setlocale(LC_ALL, NULL);
    if (!is_c(held) || !is_c(setlocale(LC_ALL, "C")) ||
        !is_c(setlocale(LC_ALL, "POSIX")) || errno != EIO) return 1;
    for (index = 0; index < sizeof(unsupported)/sizeof(unsupported[0]); ++index) {
        if (setlocale(LC_ALL, unsupported[index]) != NULL || errno != EIO ||
            !is_c(setlocale(LC_ALL, NULL)) || !is_c(held)) return 2;
    }
    if (setlocale(LC_ALL + 1, "C") != NULL ||
        setlocale(-1, NULL) != NULL || setlocale(999, "") != NULL ||
        errno != EIO || !is_c(setlocale(LC_ALL, NULL))) return 3;
    return 0;
}

/* The runtime harness supplies task environments and an independent expected
 * outcome. This ordinary source never reads the host's environment. */
int locale_environment_main(int argc, char **argv)
{
    char *result;
    const char *held;
    int success;
    if (argc != 2) return 10;
    success = argv[1][0] == '1';
    errno = EIO;
    held = setlocale(LC_ALL, NULL);
    result = setlocale(LC_ALL, "");
    if ((result != NULL) != success || (success && !is_c(result)) ||
        errno != EIO || !is_c(held) || !is_c(setlocale(LC_ALL, NULL))) return 11;
    if (!is_c(setlocale(LC_ALL, "C")) || !is_c(setlocale(LC_ALL, "POSIX")) ||
        errno != EIO) return 12;
    return 0;
}
