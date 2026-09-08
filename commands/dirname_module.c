#include "cannedbsd/abi.h"
#include <stddef.h>

extern int cb_dirname_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[]);

const struct cb_program_v1 cb_dirname_program = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_program_v1),
    "dirname",
    0,
    64 * 1024,
    cb_dirname_main
};
