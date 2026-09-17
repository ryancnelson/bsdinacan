/* Ordinary source: only the private libc headers, no runtime spies. */
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    static const struct {
        const char *format;
        int value;
        int count;
    } cases[] = {
        {"%d", 0, 1}, {"%4d", 42, 4}, {"%4d", -42, 4},
        {"%1d", 42, 2}, {"%1d", -42, 3},
        {"%32d", 42, 32}, {"%32d", -42, 32},
        {"%d", INT_MIN, 11}, {"%d", INT_MAX, 10},
        {"%1d", INT_MIN, 11}, {"%1d", INT_MAX, 10},
        {"%4d:%s", -42, 9},
        /* CAT-01 extension: width-qualified %s, originally excluded from
           FORMAT-01-design.md on the grounds uniq never needed it. cat -b
           measured otherwise -- see notes/iterations/FORMAT-01.md. Right-
           justifies like %d: pads when shorter ("tail" -> "  tail" at
           width 6), never truncates when already wider than the field
           ("tail" at width 1 stays "tail", same non-truncating policy
           %d already uses). */
        {"%4d:%6s", -42, 11},
        {"%4d:%1s", -42, 9}
    };
    static const char *const bad[] = {
        "prefix:%33d", "prefix:%999999d", "prefix:%04d", "prefix:%-4d",
        "prefix:%+d", "prefix:% d", "prefix:%#d", "prefix:%.1d",
        "prefix:%ld", "prefix:%hd", "prefix:%zd", "prefix:%jd",
        "prefix:%x", "prefix:%f", "prefix:%p", "prefix:%c",
        "prefix:%0d", "prefix:%", "prefix:%999999999999999999999999999d",
        "prefix:%4%", "prefix:%*d", "prefix:%u", "prefix:%-4s",
        "prefix:%.1s", "prefix:%33s"
    };
    unsigned index;
    int count;
    if (argc != 3) return 90;
    index = (unsigned char)argv[2][0] - (unsigned)'A';
    errno = ENOENT;
    if (argv[1][0] == 'v') {
        if (index >= sizeof(cases) / sizeof(cases[0])) return 91;
        count = fprintf(index & 1 ? stderr : stdout, cases[index].format,
                        cases[index].value, "tail");
        if (count != cases[index].count || errno != ENOENT) return 10;
        return ferror(stdout) || ferror(stderr) ? 11 : 0;
    }
    if (argv[1][0] == 'b') {
        if (index >= sizeof(bad) / sizeof(bad[0])) return 92;
        /* Rejected conversions must not read their variadic arguments. */
        count = printf(bad[index]);
        if (count != EOF || errno != EINVAL) return 12;
        return ferror(stdout) || ferror(stderr) ? 13 : 0;
    }
    if (argv[1][0] == 'f') {
        count = printf("a%db", 42);
        if (count != EOF || errno != (index == 0 ? EBADF : EPIPE)) return 14;
        return ferror(stdout) && !ferror(stderr) ? 0 : 15;
    }
    if (argv[1][0] == 's') {
        count = printf("%4d:%s", -42, "tail");
        return count == 9 && errno == ENOENT ? 0 : 16;
    }
    if (argv[1][0] == 'n') {
        /* Exact shape of pinned cat.c's cook_buf() blank-line-
           continuation call under -b: fprintf(stdout, "%6s\t", ""). */
        count = fprintf(stdout, "%6s\t", "");
        return count == 7 && errno == ENOENT ? 0 : 18;
    }
    if (argv[1][0] == 'm') {
        /* Multiple width-qualified conversions of DIFFERING widths in
           one output line -- the shape ls -l's column alignment and
           wc's multi-count output need, not just a single field. Each
           field's width is independent: "%3d" of 1 -> "  1" (pad 2),
           "%6d" of 22 -> "    22" (pad 4), "%8s" of "tail" -> "    tail"
           (pad 4), "%1d" of 333 -> "333" (no pad, wider than field). */
        count = fprintf(stdout, "%3d%6d%8s%1d", 1, 22, "tail", 333);
        return count == 20 && errno == ENOENT ? 0 : 19;
    }
    if (argv[1][0] == 'w') {
        errno = EBADF;
        warn("value:%4d", -42);
        return errno == EBADF ? 0 : 17;
    }
    if (argv[1][0] == 'e') {
        errno = EBADF;
        err(23, "value:%4d", -42);
    }
    return 93;
}
