/* Ordinary source: only the private libc headers, no runtime spies. */
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "fixtures/uniq_obsolete.h"

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
        "prefix:%hd", "prefix:%zd", "prefix:%jd",
        "prefix:%x", "prefix:%f", "prefix:%p", "prefix:%c",
        "prefix:%0d", "prefix:%", "prefix:%999999999999999999999999999d",
        "prefix:%4%", "prefix:%o",
        "prefix:%.1s", "prefix:%33s",
        /* LS-02: "'" (thousands separator) is only valid paired with
           %u/%llu/%lu (pinned ls/print.c's own "%'*llu " and
           "total %'llu\n" -- see cb_libc.c's format_output); %'d and
           %'s must still be rejected. %u/%ld/%lld/%*d/%-4s became valid
           conversions under LS-02 (pinned ls needs %*lld, bare %llu,
           %*llu, %*lu, and left-justified %s -- the left-justify check
           does not distinguish a literal digit width from a "*" one,
           so a literal-width left-justify is valid too, not just the
           "*" shape ls.c itself happens to use) and were removed from
           this table rather than kept here to be "rejected" --
           exercising a now-valid conversion through this bare
           printf(bad[index]) call (no corresponding variadic argument
           at all) would itself be undefined behavior, not a real
           boundary test. */
        "prefix:%'d", "prefix:%'s"
    };
    unsigned index;
    int count;
    if (argc != 3) return 90;
    index = (unsigned char)argv[2][0] - (unsigned)'A';
    errno = ENOENT;
    if (argv[1][0] == 'a') {
        char *p = NULL;
        char *legacy[] = {(char *)"uniq", (char *)"-3", (char *)"+3", NULL};
        char *stop[] = {(char *)"uniq", (char *)"--", (char *)"-3", NULL};
        char *operand[] = {(char *)"uniq", (char *)"file", (char *)"+3", NULL};
        char *flag[] = {(char *)"uniq", (char *)"-c", NULL};
        obsolete(legacy);
        if (strcmp(legacy[1], "-f3") || strcmp(legacy[2], "-s3")) return 46;
        free(legacy[1]); free(legacy[2]);
        obsolete(stop); obsolete(operand); obsolete(flag);
        if (strcmp(stop[2], "-3") || strcmp(operand[2], "+3") ||
            strcmp(flag[1], "-c")) return 47;
        const char *unsupported = "prefix:%d";
        static const char expected[] = {'A', 0, 'B', 'x', 0};
        if (asprintf(&p, "-%c%s", 'f', "3") != 3 ||
            p == NULL || strcmp(p, "-f3") != 0) return 40;
        free(p);
        if (asprintf(&p, "-%c%s", 's', "3") != 3 ||
            p == NULL || strcmp(p, "-s3") != 0) return 41;
        free(p);
        if (asprintf(&p, "A%cB%s", 0, "x") != 4 ||
            p == NULL || memcmp(p, expected, sizeof(expected))) return 42;
        free(p);
        if (asprintf(&p, "") != 0 || p == NULL || *p) return 43;
        free(p);
        if (errno != ENOENT) return 44;
        p = (char *)"sentinel";
        if (asprintf(&p, unsupported, 0) != -1 || p != NULL ||
            errno != EINVAL) return 45;
        return 0;
    }
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
    if (argv[1][0] == 'u') {
        /* LS-02: %u/%llu/%*llu/%*lu, the shapes pinned ls/print.c's
           "%*"PRIu64" ", "%*llu ", "%*lu ", and "total %llu\n" call
           sites actually use -- correctly typed variadic arguments,
           unlike the int/const-char*-only cases[] table above, since
           these need a real unsigned long long. */
        count = printf("%u %llu %*llu %*lu", 7U, 12345ULL, 6, 42ULL, 3, 8UL);
        return count == 18 && errno == ENOENT ? 0 : 20;
    }
    if (argv[1][0] == 'l') {
        /* %*lld, the shape "%*lld, %*lld " (block/major/minor device
           columns) needs -- a signed conversion with a dynamic width. */
        count = printf("%*lld", 5, -42LL);
        return count == 5 && errno == ENOENT ? 0 : 21;
    }
    if (argv[1][0] == 'c') {
        /* The "'" flag ls -M's "%'*llu " and "total %'llu\n" call
           sites use. The C locale's thousands_sep is empty, so POSIX
           grouping inserts nothing (STATICS-CACHE-02 replaced an
           earlier comma-grouping assertion that did not match NetBSD
           printf under LANG=C). */
        count = printf("%'llu|%'*llu|%'llu", 1234567ULL, 8, 999ULL, 42ULL);
        return count == 19 && errno == ENOENT ? 0 : 22;
    }
    if (argv[1][0] == 'z') {
        size_t maximum = argv[2][0] == 'P' ? (size_t)UINT32_MAX : (size_t)-1;
        const char *invalid[] = {"%zd", "%zs", "%zz", "%zl", "%z", "%zx"};
        /* Real size_t operands, followed by differently typed arguments:
           a wrong va_arg width must not consume or corrupt the next field. */
        count = printf("%zu|%zu|%u|%s", (size_t)0, maximum, 7U, "end");
        if (count != (argv[2][0] == 'P' || sizeof(size_t) == 4 ? 18 : 28)) return 29;
        count = fprintf(stderr, "|%6zu|%1zu|%*zu", (size_t)42,
                        (size_t)123, 4, (size_t)9);
        if (count != 16 || errno != ENOENT) return 30;
        for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            /* No operands: reject unsupported z combinations before va_arg. */
            errno = ENOENT;
            if (printf(invalid[index]) != EOF || errno != EINVAL) return 31;
        }
        return ferror(stdout) || ferror(stderr) ? 32 : 0;
    }
    if (argv[1][0] == 'q') {
        /* Typed arguments: same PRI macros as ls and humanize_number.
           Exercise both formatter sinks and preserve l/ll independently. */
        char buffer[160];
        const char *expected = "0 18446744073709551615 -9223372036854775808 9223372036854775807";
        uint64_t zero = 0, umax = UINT64_MAX;
        int64_t smin = INT64_MIN, smax = INT64_MAX;
        count = snprintf(buffer, sizeof(buffer), "%" PRIu64 " %" PRIu64
                         " %" PRId64 " %" PRId64, zero, umax, smin, smax);
        if (count != (int)strlen(expected) || strcmp(buffer, expected)) return 24;
        count = printf("%" PRIu64 " %" PRIu64 " %" PRId64 " %" PRId64,
                       zero, umax, smin, smax);
        if (count != (int)strlen(expected)) return 25;
        count = fprintf(stderr, "|%ld %lu %lld %llu", -42L, 42UL, -42LL, 42ULL);
        if (count != 14) return 26;
        count = snprintf(buffer, sizeof(buffer), "%ld %lu %lld %llu",
                         -42L, 42UL, -42LL, 42ULL);
        if (count != 13 || strcmp(buffer, "-42 42 -42 42")) return 27;
        return errno == ENOENT ? 0 : 28;
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
