#include "cannedbsd/libc.h"

extern int cb_progname_probe_main(int argc, char **argv);

static int progname_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    const char *identity;
    const char *expected = argc > 1 ? argv[1] : "libcprognameprobe";
    int result = cb_libc_start(api, argc, argv, cb_progname_probe_main);
    int status;
    cb_pid_t child;
    size_t index;
    char *failed_argv[] = {(char *)"must-not-replace-identity", NULL};
    char *exec_argv[] = {(char *)"/after/replacement", (char *)"replacement", NULL};
    char *cases[][4] = {
        {(char *)"plain", (char *)"plain", NULL, NULL},
        {(char *)"/one/leaf", (char *)"leaf", NULL, NULL},
        {(char *)"/last/", (char *)"", NULL, NULL},
        {(char *)"", (char *)"", NULL, NULL},
        {(char *)"////", (char *)"", NULL, NULL},
        {(char *)"/before/original", (char *)"original", (char *)"exec", NULL}
    };
    if (result != 0)
        return 10 + result;
    identity = cb_libc_getprogname();
    if (api->exec("progname_missing_command", failed_argv, envp) >= 0 ||
        api->get_errno() != CB_ENOENT)
        return 20;
    if (cb_libc_getprogname() != identity ||
        cb_libc_strcmp(identity, expected) != 0)
        return 21;

    if (argc > 1) {
        api->yield();
        if (cb_libc_getprogname() != identity ||
            cb_libc_strcmp(identity, expected) != 0 ||
            cb_libc_start(api, argc, argv, cb_progname_probe_main) != 0)
            return 22;
        if (argc == 3) {
            api->exec("libcprognameprobe", exec_argv, envp);
            return 23; /* successful exec must not return */
        }
        return 0;
    }

    /* Parent retains its borrowed result across child calls, yields and reap.
       Each child also checks its result before and after a real scheduler turn.
       No test-global saved name can disguise cross-task contamination. */
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (api->spawn("libcprognameprobe", cases[index], envp,
                       NULL, 0, &child) < 0)
            return 30;
        api->yield();
        if (cb_libc_getprogname() != identity ||
            cb_libc_strcmp(identity, expected) != 0)
            return 31;
        if (api->waitpid(child, &status) != child || status != 0)
            return 32;
        if (cb_libc_getprogname() != identity ||
            cb_libc_strcmp(identity, expected) != 0)
            return 33;
    }
    return 0;
}

const struct cb_program_v1 cb_progname_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "libcprognameprobe", 0,
    64 * 1024, progname_main
};
