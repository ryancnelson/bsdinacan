#include <stdio.h>

/* CAT-01/STDIN-01 addendum: fclose(stdout)/fclose(stderr) now succeed, but
   must do so honestly -- a write after fclose must genuinely fail, not
   silently succeed, or the success return would be a lie. */
int main(void)
{
    if (fprintf(stdout, "before\n") < 0)
        return 1;
    if (fclose(stdout) != 0)
        return 2;
    if (fprintf(stdout, "after\n") >= 0)
        return 3;
    if (fclose(stdout) >= 0)
        return 4;
    if (fprintf(stderr, "before\n") < 0)
        return 5;
    if (fclose(stderr) != 0)
        return 6;
    if (fprintf(stderr, "after\n") >= 0)
        return 7;
    if (fclose(stderr) >= 0)
        return 8;
    return 0;
}
