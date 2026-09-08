#include "cannedbsd/libc.h"

int cb_head_main(int argc, char *argv[]);

static int head_start(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    (void)envp;
    return cb_libc_start(api, argc, argv, cb_head_main);
}

/* The pinned source has a 65536-byte automatic buffer. This per-command
   budget leaves room for its call chain without changing global defaults. */
const struct cb_program_v1 cb_head_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "head", 0,
    128 * 1024, head_start
};
