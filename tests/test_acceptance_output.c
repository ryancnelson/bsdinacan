#include "../platform/mac68k/acceptance_output.h"
#include <stdio.h>

int main(void)
{
    const char hidden[] = {0, (char)0xff, 'A', 'B', 0};
    const char suffix[] = {'o', 'k', 0, 'x', 0};
    if (cb_acceptance_output_matches(hidden, 4, 0, "")) {
        fputs("FAIL: hidden binary bytes accepted as empty output\n", stderr);
        return 1;
    }
    if (cb_acceptance_output_matches(suffix, 4, 0, "ok")) return 2;
    if (cb_acceptance_output_matches("ok", 2, 1, "ok")) return 3;
    if (cb_acceptance_output_matches("o", 1, 0, "ok")) return 4;
    if (cb_acceptance_output_matches("no", 2, 0, "ok")) return 5;
    if (!cb_acceptance_output_matches("ok", 2, 0, "ok")) return 6;
    if (!cb_acceptance_output_matches("", 0, 0, "")) return 7;
    {
        const char raw[] = {'o', 'k'};
        if (!cb_acceptance_output_matches(raw, sizeof(raw), 0, "ok")) return 8;
    }
    puts("acceptance byte-length checks passed");
    return 0;
}
