#include "cannedbsd/libc.h"

int cb_wc_main(int argc, char *argv[]);

static int wc_start(const struct cb_api_v1 *api, int argc,
                    char *const argv[], char *const envp[])
{
    (void)envp;
    return cb_libc_start(api, argc, argv, cb_wc_main);
}

/* The pinned source has two MAXBSIZE automatic buffers (buf[65536] and
   wbuf[65536]), requiring up to 320KiB stack. This per-command budget
   leaves room for its call chain without changing global defaults. */
const struct cb_program_v1 cb_wc_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "wc", 0,
    512 * 1024, wc_start
};
