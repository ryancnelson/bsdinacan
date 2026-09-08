#include "cannedbsd/abi.h"
#include <stddef.h>

extern int cb_stdio_oldtable_main(int argc, char **argv);
extern int cb_stdio_state_probe_main(int argc, char **argv);

/* Need to declare cb_libc_start since we are bypassing the header */
int cb_libc_start(const struct cb_api_v1 *api, int argc,
                  char *const argv[],
                  int (*main_function)(int, char **));

static struct cb_stdio_state_v1 short_state;
static struct cb_stdio_state_v1 *short_state_location(void) {
    short_state.struct_size = 0; /* short state */
    return &short_state;
}

static struct cb_stdio_state_v1 wrong_version_state;
static struct cb_stdio_state_v1 *wrong_version_location(void) {
    wrong_version_state.abi_version = CB_ABI_VERSION_V1 + 1;
    wrong_version_state.struct_size = sizeof(wrong_version_state);
    return &wrong_version_state;
}

static int stdiovariants_main(const struct cb_api_v1 *api, int argc, char *const argv[], char *const envp[])
{
    struct cb_api_v1 copy;
    int result;
    (void)envp;
    
    copy = *api;
    copy.struct_size = (uint32_t)offsetof(struct cb_api_v1, stdio_state_location);
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (result != 0) return 492;
    
    copy = *api;
    copy.stdio_state_location = NULL;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (result != 0) return 493;
    
    copy = *api;
    copy.stdio_state_location = short_state_location;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (result != 0) return 495;

    copy = *api;
    copy.stdio_state_location = wrong_version_location;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (result != 0) return 496;

    char *rebind_args[] = {"rebindprobe", "rebind", NULL};
    char *persist_args[] = {"rebindprobe", "rebind_persist", NULL};

    /* Set sticky error on stdout using full API */
    result = cb_libc_start(api, 2, rebind_args, cb_stdio_state_probe_main);
    if (result != 0) return 497;

    /* Verify it is inaccessible/ENOSYS via old API */
    copy = *api;
    copy.stdio_state_location = NULL;
    result = cb_libc_start(&copy, argc, argv, cb_stdio_oldtable_main);
    if (result != 0) return 498;

    /* Verify it is STILL sticky via full API */
    result = cb_libc_start(api, 2, persist_args, cb_stdio_state_probe_main);
    if (result != 0) return 499;

    return 0;
}

const struct cb_program_v1 cb_stdio_oldtable_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdiooldtable", 0,
    64 * 1024, stdiovariants_main
};
