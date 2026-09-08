#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_argv_probe_main(int, char **);
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    static const char *modes[] = {"exit", "shorten", "exec", "failed"};
    char *child_args[] = {(char *)"mutate", (char *)"original", NULL, NULL};
    char *next[] = {(char *)"after", NULL, NULL};
    cb_pid_t child;
    int status;
    size_t i;
    const char *startup = api->getprogname();
    if (strcmp(argv[0], "after") == 0)
        return argc == 2 && strcmp(argv[1], "replacement argument") == 0 &&
               strcmp(startup, "after") == 0 ? 0 : 1;
    if (strcmp(argv[0], "mutate") == 0) {
        if (argc != 3 || cb_libc_start(api, argc, argv, cb_argv_probe_main) != 0)
            return 2;
        next[1] = argv[1];
        if (strcmp(argv[2], "shorten") == 0) ((char **)argv)[0] = NULL;
        if (api->getprogname() != startup || strcmp(startup, "mutate") != 0)
            return 3;
        if (strcmp(argv[2], "exec") == 0) {
            api->exec("argvprobe", next, envp);
            return 4;
        }
        if (strcmp(argv[2], "failed") == 0 &&
            (api->exec("missing-argv-command", next, envp) != -1 ||
             api->get_errno() != CB_ENOENT)) return 5;
        return 0;
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        child_args[2] = (char *)modes[i];
        if (api->spawn("argvprobe", child_args, envp, NULL, 0, &child) != 0 ||
            api->waitpid(child, &status) != child || status != 0) return 6;
    }
    return 0;
}
const struct cb_program_v1 cb_argv_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "argvprobe", 0,
    64 * 1024, entry
};
