#include <stdio.h>

/* console_input fed by the caller (getcharprobe_main in tests/test_core.c)
   before invoking this through cb_libc_start. getchar() is defined as a
   plain macro, getc(stdin) -- this proves that composition, not getc()
   itself (already covered elsewhere). */
int cb_getchar_walk_main(int argc, char *argv[])
{
    int ch;
    (void)argc;
    (void)argv;

    ch = getchar();
    if (ch != 'h')
        return 1;
    ch = getchar();
    if (ch != 'i')
        return 2;
    ch = getchar();
    if (ch != EOF)
        return 3;
    return 0;
}
