#include "cannedbsd/libc.h"
#include <string.h>

extern int cb_stdin_probe_main(int, char **);
int cb_stdin_call(const struct cb_api_v1 *api, const char *mode)
{
    char *args[] = {(char *)"stdinprobe", (char *)mode, NULL};
    return cb_libc_start(api, 2, args, cb_stdin_probe_main);
}
static int prepare(const struct cb_api_v1 *api)
{
    static const unsigned char bytes[] = {0, 255, 'A'};
    int fd = api->open("/tmp/stdin-probe", CB_O_CREAT | CB_O_TRUNC | CB_O_RDWR, 0600);
    if (fd < 0) return -1;
    if (api->write(fd, bytes, sizeof(bytes)) != (cb_ssize_t)sizeof(bytes) ||
        api->lseek(fd, 0, CB_SEEK_SET) != 0 || api->dup2(fd, 0) != 0) {
        api->close(fd); return -1;
    }
    return fd == 0 ? 0 : api->close(fd);
}
static int entry(const struct cb_api_v1 *api, int argc,
                  char *const argv[], char *const envp[])
{
    char *peer[] = {(char *)"stdinprobe", (char *)"peer-error", NULL};
    char *error_exec[] = {(char *)"stdinprobe", (char *)"exec-error", NULL};
    char *after[] = {(char *)"stdinprobe", (char *)"after", NULL};
    cb_pid_t child;
    int status;
    if (argc == 2 && strcmp(argv[1], "after") == 0) {
        if (cb_stdin_call(api, "clean") != 0) return 31;
        return cb_stdin_call(api, "binary");
    }
    if (argc == 2 && strcmp(argv[1], "peer-error") == 0) {
        if (cb_stdin_call(api, "clean") != 0) return 32;
        return cb_stdin_call(api, "badfd");
    }
    if (argc == 2 && strcmp(argv[1], "exec-error") == 0) {
        if (cb_stdin_call(api, "clean") != 0 || cb_stdin_call(api, "badfd") != 0)
            return 33;
        if (api->exec("missing-stdin-command", after, envp) != -1 ||
            api->get_errno() != CB_ENOENT ||
            cb_stdin_call(api, "error-retained") != 0) return 42;
        if (prepare(api) != 0 || cb_stdin_call(api, "error-retained") != 0 ||
            cb_stdin_call(api, "rebound") != 0 || api->lseek(0, 0, CB_SEEK_SET) != 0)
            return 34;
        api->exec("stdinprobe", after, envp);
        return 35;
    }
    if (prepare(api) != 0 || cb_stdin_call(api, "clean") != 0 ||
        cb_stdin_call(api, "binary") != 0) return 36;
    if (api->spawn("stdinprobe", peer, envp, NULL, 0, &child) != 0 ||
        api->waitpid(child, &status) != child || status != 0 ||
        cb_stdin_call(api, "eof-retained") != 0) return 37;
    if (api->exec("missing-stdin-command", after, envp) != -1 ||
        api->get_errno() != CB_ENOENT || cb_stdin_call(api, "eof-retained") != 0)
        return 38;
    if (api->spawn("stdinprobe", error_exec, envp, NULL, 0, &child) != 0 ||
        api->waitpid(child, &status) != child || status != 0 ||
        cb_stdin_call(api, "eof-retained") != 0) return 39;
    if (api->lseek(0, 0, CB_SEEK_SET) != 0) return 40;
    api->exec("stdinprobe", after, envp);
    return 41;
}
const struct cb_program_v1 cb_stdin_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "stdinprobe", 0,
    64 * 1024, entry
};
