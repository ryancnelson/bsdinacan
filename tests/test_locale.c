#include "internal.h"
#include "cannedbsd/libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int locale_environment_main(int argc, char **argv);
extern const struct cb_program_v1 cb_locale_probe_program;
static const char *const variables[] = {
    "LC_ALL", "LANG", "LC_COLLATE", "LC_CTYPE", "LC_MONETARY",
    "LC_NUMERIC", "LC_TIME", "LC_MESSAGES"
};

static int env_check(const struct cb_api_v1 *api, int expected)
{
    char *argv[] = {(char *)"locale", expected ? (char *)"1" : (char *)"0", NULL};
    return cb_libc_start(api, 2, argv, locale_environment_main);
}

static int peer_main(const struct cb_api_v1 *api, int argc,
                     char *const argv[], char *const envp[])
{
    const char *held;
    int result;
    (void)envp;
    result = cb_libc_start(api, argc, argv, locale_environment_main);
    if (result != 0) return result;
    held = cb_libc_setlocale(CB_LIBC_LC_ALL, NULL);
    api->yield();
    if (held == NULL || strcmp(held, "C") != 0) return 13;
    return cb_libc_start(api, argc, argv, locale_environment_main);
}

static int contract_main(const struct cb_api_v1 *api, int argc,
                         char *const argv[], char *const envp[])
{
    struct cb_api_v1 modified;
    cb_pid_t first, second;
    int status, result;
    size_t index;
    char *accept_argv[] = {(char *)"localepeer", (char *)"1", NULL};
    char *reject_argv[] = {(char *)"localepeer", (char *)"0", NULL};
    char *accept_env[] = {(char *)"LC_ALL=C", (char *)"LANG=en_US.UTF-8",
                          (char *)"LC_CTYPE=unsupported", NULL};
    char *reject_env[] = {(char *)"LANG=C", (char *)"LC_TIME=unsupported", NULL};
    (void)argc; (void)argv; (void)envp;
    for (index = 0; index < sizeof(variables)/sizeof(variables[0]); ++index)
        if (api->unsetenv(variables[index]) != 0) return 20;
    if (env_check(api, 1) != 0) return 21;
    /* Empty values fall through; LC_ALL overrides even non-C categories/LANG. */
    if (api->setenv("LC_ALL", "", 1) != 0 || api->setenv("LANG", "", 1) != 0 ||
        api->setenv("LC_CTYPE", "", 1) != 0 || env_check(api, 1) != 0 ||
        api->setenv("LANG", "POSIX", 1) != 0 || env_check(api, 1) != 0 ||
        api->setenv("LANG", "en_US.UTF-8", 1) != 0 || env_check(api, 0) != 0 ||
        api->setenv("LC_CTYPE", "unsupported", 1) != 0 ||
        api->setenv("LC_ALL", "C", 1) != 0 || env_check(api, 1) != 0 ||
        api->setenv("LC_ALL", "POSIX", 1) != 0 || env_check(api, 1) != 0 ||
        api->setenv("LANG", "C", 1) != 0 ||
        api->setenv("LC_ALL", "unsupported", 1) != 0 || env_check(api, 0) != 0 ||
        api->setenv("LC_ALL", "", 1) != 0) return 22;
    /* Each category must participate in the LC_ALL selection. */
    for (index = 2; index < sizeof(variables)/sizeof(variables[0]); ++index)
        if (api->setenv(variables[index], "C", 1) != 0) return 23;
    if (api->setenv("LANG", "unsupported", 1) != 0 || env_check(api, 1) != 0)
        return 24;
    for (index = 2; index < sizeof(variables)/sizeof(variables[0]); ++index) {
        if (api->setenv(variables[index], "", 1) != 0 || env_check(api, 0) != 0 ||
            api->setenv(variables[index], "POSIX", 1) != 0 || env_check(api, 1) != 0)
            return 25;
    }
    if (api->setenv("LANG", "C", 1) != 0) return 26;
    for (index = 2; index < sizeof(variables)/sizeof(variables[0]); ++index) {
        if (api->setenv(variables[index], "unsupported", 1) != 0 ||
            env_check(api, 0) != 0 || api->setenv(variables[index], "", 1) != 0 ||
            env_check(api, 1) != 0) return 27;
    }
    modified = *api;
    modified.struct_size = offsetof(struct cb_api_v1, poll);
    if (env_check(&modified, 1) != 0) return 28;
    modified = *api;
    modified.getenv = NULL;
    if (env_check(&modified, 0) != 0) return 29;
    if (env_check(api, 1) != 0) return 30;
    /* Interleave tasks with contradictory selections, checking both statuses. */
    if (api->spawn("localepeer", accept_argv, accept_env, NULL, 0, &first) != 0 ||
        api->spawn("localepeer", reject_argv, reject_env, NULL, 0, &second) != 0)
        return 31;
    result = api->waitpid(first, &status);
    if (result != first || status != 0) return 32;
    result = api->waitpid(second, &status);
    if (result != second || status != 0) return 33;
    return env_check(api, 1);
}

static const struct cb_program_v1 contract = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "localecontract", 0,
    64 * 1024, contract_main
};
static const struct cb_program_v1 peer = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "localepeer", 0,
    64 * 1024, peer_main
};

void cb_test_locale(void)
{
    const char *commands[] = {"libclocaleprobe", "localecontract"};
    struct cb_kernel *kernel;
    size_t index;
    int status;
    for (index = 0; index < sizeof(commands)/sizeof(commands[0]); ++index) {
        kernel = cb_kernel_create(cb_linux_host_ops());
        if (kernel == NULL) abort();
        cb_register_base_programs(kernel);
        if (cb_kernel_register(kernel, &cb_locale_probe_program) != 0 ||
            cb_kernel_register(kernel, &contract) != 0 ||
            cb_kernel_register(kernel, &peer) != 0 ||
            cb_kernel_boot(kernel, commands[index]) != 0) abort();
        status = cb_kernel_run(kernel);
        cb_kernel_destroy(kernel);
        if (status != 0) {
            fprintf(stderr, "locale test %s returned %d\n", commands[index], status);
            exit(1);
        }
    }
}
