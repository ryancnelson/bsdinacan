#include <stdio.h>

/* CAT-01/STDIN-01 addendum: fclose(stdout)/fclose(stderr) now succeed, but
   must do so honestly -- a write after fclose must genuinely fail, not
   silently succeed, or the success return would be a lie. */
/* STATICS-RESET-01: `int main(void)` was called through cb_libc_main_fn
   (`int (*)(int, char **)`, see cb_libc_start()/libc_fclose_stdout_probe_
   module.c's own extern prototype) -- undefined behavior per the C
   standard, benign only by luck of the ABI on the platforms this has
   been tested on so far, and mac68k is a real target where a mismatched
   calling convention is not free. Caught by -fsanitize=function once a
   prior heap-use-after-free elsewhere stopped being fatal early enough
   for this probe to actually run; matches the real signature every
   caller already assumes it has. */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
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
