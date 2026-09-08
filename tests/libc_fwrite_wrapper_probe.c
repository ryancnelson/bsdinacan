#include "cannedbsd/abi.h"
#include <string.h>

int cb_fwrite_wrapper_main(const struct cb_api_v1 *api, int argc,
                           char *const argv[], char *const envp[])
{
    int out_pipe[2];
    int err_pipe[2];
    cb_pid_t child;
    int status;
    char out_buf[10];
    char err_buf[10];
    cb_ssize_t out_len, err_len;
    struct cb_spawn_action_v1 actions[2];
    char *child_argv[] = {(char *)"fwrite_probe", (char *)"ordinary", NULL};

    (void)argc;
    (void)argv;

    if (api->pipe(out_pipe) < 0) return 1;
    if (api->pipe(err_pipe) < 0) return 2;

    memset(actions, 0, sizeof(actions));
    actions[0].abi_version = CB_ABI_VERSION_V1;
    actions[0].struct_size = sizeof(struct cb_spawn_action_v1);
    actions[0].type = CB_SPAWN_DUP2;
    actions[0].from_fd = out_pipe[1];
    actions[0].to_fd = 1;

    actions[1].abi_version = CB_ABI_VERSION_V1;
    actions[1].struct_size = sizeof(struct cb_spawn_action_v1);
    actions[1].type = CB_SPAWN_DUP2;
    actions[1].from_fd = err_pipe[1];
    actions[1].to_fd = 2;

    if (api->spawn("fwrite_probe", child_argv, envp, actions, 2, &child) < 0) return 3;

    api->close(out_pipe[1]);
    api->close(err_pipe[1]);

    if (api->waitpid(child, &status) != child) return 4;
    if (status != 0) return 5;

    out_len = api->read(out_pipe[0], out_buf, sizeof(out_buf));
    err_len = api->read(err_pipe[0], err_buf, sizeof(err_buf));

    if (out_len != 4) return 6;
    if (err_len != 5) return 7;

    if (memcmp(out_buf, "\x00\xff""AB", 4) != 0) return 8;
    if (memcmp(err_buf, "error", 5) != 0) return 9;

    api->write(1, "PASS\n", 5);
    return 0;
}

const struct cb_program_v1 cb_fwrite_wrapper_program = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_program_v1),
    "fwrite_wrapper",
    0,
    64 * 1024,
    cb_fwrite_wrapper_main
};
