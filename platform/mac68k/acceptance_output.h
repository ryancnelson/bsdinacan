#ifndef CB_ACCEPTANCE_OUTPUT_H
#define CB_ACCEPTANCE_OUTPUT_H

#include <stddef.h>
#include <string.h>

static int cb_acceptance_output_matches(const char *actual, size_t actual_size,
                                       int truncated, const char *expected)
{
    size_t expected_size = strlen(expected);
    return !truncated && actual_size == expected_size &&
           memcmp(actual, expected, expected_size) == 0;
}

#endif
