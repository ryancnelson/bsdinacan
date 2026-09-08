#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

/* A value none of this project's real CB_E* codes equal, seeded before
   every case whose contract says errno stays untouched: verifies that
   claim bit-for-bit rather than merely reading back as zero, which would
   not actually distinguish "never touched" from "touched and reset." */
#define SENTINEL 0x5a5a5a5a

static int check_case(const char *input, int base, intmax_t expected_value,
                      int endptr_offset, int expected_errno)
{
    char *endptr = NULL;
    intmax_t result;
    errno = SENTINEL;
    result = strtoimax(input, &endptr, base);
    if (result != expected_value)
        return 0;
    if (endptr_offset < 0) {
        if (endptr != input)
            return 0;
    } else {
        if (endptr != input + endptr_offset)
            return 0;
    }
    if (expected_errno == 0) {
        if (errno != SENTINEL)
            return 0;
    } else {
        if (errno != expected_errno)
            return 0;
    }
    return 1;
}

static int check_null_endptr(void)
{
    intmax_t result;
    errno = SENTINEL;
    result = strtoimax("5", NULL, 10);
    if (result != 5)
        return 0;
    if (errno != SENTINEL)
        return 0;
    return 1;
}

static int check_overflow_continuation(void)
{
    char buffer[40];
    char *endptr;
    intmax_t result;
    size_t nines = 30;
    size_t index;
    for (index = 0; index < nines; ++index)
        buffer[index] = '9';
    buffer[nines] = 'a';
    buffer[nines + 1] = 'b';
    buffer[nines + 2] = 'c';
    buffer[nines + 3] = '\0';
    errno = SENTINEL;
    result = strtoimax(buffer, &endptr, 10);
    if (result != INTMAX_MAX)
        return 0;
    /* The loop keeps consuming recognized digits after overflow latches
       (_strtol.h's `if (any < 0) continue;`), so endptr must scan past
       every '9', not stop at the point overflow was first detected. */
    if (endptr != buffer + nines)
        return 0;
    if (errno != ERANGE)
        return 0;
    return 1;
}

static int check_ctype(void)
{
    int value;
    for (value = 0; value <= 255; ++value) {
        int expect_digit = value >= '0' && value <= '9';
        int expect_space = value == ' ' || value == '\t' || value == '\n' ||
                           value == '\v' || value == '\f' || value == '\r';
        if ((isdigit(value) != 0) != expect_digit)
            return 0;
        if ((isspace(value) != 0) != expect_space)
            return 0;
    }
    if (isdigit(-1) != 0)
        return 0;
    if (isspace(-1) != 0)
        return 0;
    return 1;
}

static int check_strerror(void)
{
    const char *message = strerror(ERANGE);
    if (message == NULL || message[0] == '\0')
        return 0;
    if (strcmp(message, strerror(EINVAL)) == 0)
        return 0;
    return 1;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!check_case("10", 10, 10, 2, 0))
        return 1;
    if (!check_case("-10", 10, -10, 3, 0))
        return 2;
    if (!check_case("  42", 10, 42, 4, 0))
        return 3;
    if (!check_case("+5", 10, 5, 2, 0))
        return 4;
    if (!check_case("9223372036854775807", 10, INTMAX_MAX, 19, 0))
        return 5;
    if (!check_case("9223372036854775808", 10, INTMAX_MAX, 19, ERANGE))
        return 6;
    if (!check_case("-9223372036854775808", 10, INTMAX_MIN, 20, 0))
        return 7;
    if (!check_case("-9223372036854775809", 10, INTMAX_MIN, 20, ERANGE))
        return 8;
    if (!check_case("12abc", 10, 12, 2, 0))
        return 9;
    if (!check_case("abc", 10, 0, -1, 0))
        return 10;
    if (!check_case("", 10, 0, -1, 0))
        return 11;
    if (!check_case("0", 10, 0, 1, 0))
        return 12;
    if (!check_case("018", 10, 18, 3, 0))
        return 13;
    if (!check_case("-", 10, 0, -1, 0))
        return 14;
    if (!check_case("017", 0, 15, 3, 0))
        return 15;
    if (!check_case("0x1F", 0, 31, 4, 0))
        return 16;
    if (!check_case("42", 0, 42, 2, 0))
        return 17;
    if (!check_case("1010", 2, 10, 4, 0))
        return 18;
    if (!check_case("1f", 16, 31, 2, 0))
        return 19;
    if (!check_case("1F", 16, 31, 2, 0))
        return 20;
    if (!check_case("z", 36, 35, 1, 0))
        return 21;
    if (!check_case("Z", 36, 35, 1, 0))
        return 22;
    if (!check_case("5", 1, 0, -1, EINVAL))
        return 23;
    if (!check_case("5", 37, 0, -1, EINVAL))
        return 24;
    if (!check_null_endptr())
        return 25;
    if (!check_overflow_continuation())
        return 26;
    if (!check_ctype())
        return 27;
    if (!check_strerror())
        return 28;
    return 0;
}
