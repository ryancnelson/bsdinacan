#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_getopt_arg_probe_main(int, char **);
static int ordinary(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"getoptargs", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_getopt_arg_probe_main);
}
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    char *peer[] = {(char *)"getoptargs", (char *)"peer", NULL};
    char *seed[] = {(char *)"getoptargs", (char *)"exec-seed", NULL};
    char *after[] = {(char *)"getoptargs", (char *)"exec-after", NULL};
    cb_pid_t child;
    int status;
    unsigned i;
    if (argc != 2) return 80;
    if (strcmp(argv[1], "exec-seed") == 0 || strcmp(argv[1], "exec-cluster") == 0) {
        if (ordinary(api, argv[1]) != 0) return 81;
        api->exec("getoptargs", after, envp);
        return 82;
    }
    if (strcmp(argv[1], "lifecycle") != 0)
        return cb_libc_start(api, argc, argv, cb_getopt_arg_probe_main);
    if (ordinary(api, "owner-start") != 0) return 83;
    for (i = 0; i < 2; ++i) {
        if (api->spawn("getoptargs", peer, envp, NULL, 0, &child) != 0 ||
            api->waitpid(child, &status) != child || status != 0) return 84;
        if (ordinary(api, i == 0 ? "owner-argument" : "owner-retained") != 0)
            return 85;
    }
    if (api->exec("missing-getopt-command", after, envp) != -1 ||
        api->get_errno() != CB_ENOENT || ordinary(api, "owner-retained") != 0 ||
        ordinary(api, "owner-finish") != 0) return 86;
    for (i = 0; i < 2; ++i) {
        seed[1] = (char *)(i == 0 ? "exec-seed" : "exec-cluster");
        if (api->spawn("getoptargs", seed, envp, NULL, 0, &child) != 0 ||
            api->waitpid(child, &status) != child || status != 0) return 87;
    }
    return 0;
}
const struct cb_program_v1 cb_getopt_arg_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "getoptargs", 0,
    64 * 1024, entry
};
