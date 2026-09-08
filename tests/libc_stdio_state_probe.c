#define CANNEDBSD_SOURCE_FENCE
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int cb_stdio_state_probe_main(int argc, char **argv)
{
    if (argc < 2) return 1;
    
    if (strcmp(argv[1], "putchar_ok") == 0) {
        if (putchar('A') == EOF) return 2;
        if (fflush(stdout) == EOF) return 3;
        if (ferror(stdout)) return 4;
        return 0;
    }
    
    if (strcmp(argv[1], "putchar_fail") == 0) {
        close(1);
        if (putchar('A') != EOF) return 2;
        if (ferror(stdout) == 0) return 3;
        return 0;
    }
    
    if (strcmp(argv[1], "fflush_invalid") == 0) {
        FILE *bad = (FILE *)0xdeadbeef;
        if (fflush(bad) != EOF) return 2;
        if (errno != EINVAL) return 3;
        if (ferror(bad) == 0) return 4;
        if (errno != EINVAL) return 5;
        return 0;
    }
    
    if (strcmp(argv[1], "write_retry") == 0) {
        if (puts("hello") == EOF) return 2;
        if (ferror(stdout)) return 3;
        return 0;
    }

    if (strcmp(argv[1], "sticky_error") == 0) {
        /* Mock will fail this write */
        puts("fail_me");
        if (!ferror(stdout)) return 2;
        int old_errno = errno;
        /* Mock will NOT fail this write */
        if (puts("success") == EOF) return 3;
        /* Errno should be preserved */
        if (errno != old_errno) return 4;
        /* Sticky error is still set */
        if (!ferror(stdout)) return 5;
        /* fflush preserves errno and sticky error */
        if (fflush(stdout) == EOF) return 6;
        if (!ferror(stdout)) return 7;
        return 0;
    }

    if (strcmp(argv[1], "stderr_indep") == 0) {
        /* Mock will fail stdout */
        puts("fail_me");
        if (!ferror(stdout)) return 2;
        if (ferror(stderr)) return 3;
        
        /* But stderr should be unaffected */
        fprintf(stderr, "success_stderr");
        if (ferror(stderr)) return 4;
        return 0;
    }

    
    if (strcmp(argv[1], "rebind") == 0) {
        puts("fail_me");
        if (!ferror(stdout)) return 2;
        return 0;
    }
    
    if (strcmp(argv[1], "rebind_persist") == 0) {
        if (!ferror(stdout)) return 2;
        return 0;
    }
    return 99;
}
