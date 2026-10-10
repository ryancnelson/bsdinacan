/* Ordinary-source C/POSIX comparisons, independent of host locale. */
#include <string.h>
#include <strings.h>
#include <locale.h>
#include <errno.h>

static int casecmp_checks(void)
{
    unsigned left, right;
    char a[2], b[2];
    int (*compare)(const char *, const char *) = strcasecmp;
    errno = EIO;
    if (compare("", "") || compare("Mixed CASE", "mIXED case") ||
        compare("a", "Aa") >= 0 || compare("AA", "a") <= 0 ||
        compare("[", "Z") >= 0 || compare("A\0z", "a\0b") || errno != EIO)
        return 50;
    /* All byte pairs include empty termination and unchanged high bytes. */
    a[1] = b[1] = 0;
    for (left = 0; left <= 255; ++left) {
        unsigned folded_left = left >= 'A' && left <= 'Z' ? left + ('a'-'A') : left;
        a[0] = (char)left;
        for (right = 0; right <= 255; ++right) {
            unsigned folded_right = right >= 'A' && right <= 'Z' ? right + ('a'-'A') : right;
            int expected = (folded_left > folded_right) - (folded_left < folded_right);
            int result;
            b[0] = (char)right;
            result = compare(a, b);
            if ((result > 0) - (result < 0) != expected || errno != EIO) return 51;
        }
    }
    if (setlocale(LC_ALL, "unsupported") != NULL || compare("AZ", "az") || errno != EIO)
        return 52;
    if (setlocale(LC_ALL, "POSIX") == NULL || compare("Prefix", "prefix-more") >= 0 || errno != EIO)
        return 53;
    return 0;
}
