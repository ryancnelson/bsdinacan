#include "cannedbsd/abi.h"

#include <string.h>

static int vfs_executable_probe_main(const struct cb_api_v1 *api, int argc,
                                    char *const argv[], char *const envp[])
{
    struct cb_stat_v1 statbuf;
    cb_pid_t child;
    (void)argc; (void)argv; (void)envp;

    if (api->stat("/bin/sh", &statbuf) != 0)
        return 1;
    if (statbuf.type != CB_NODE_EXECUTABLE || statbuf.size != 0)
        return 2;

    if (api->open("/bin/sh", CB_O_WRONLY | CB_O_TRUNC, 0) >= 0)
        return 3;
    if (api->get_errno() != CB_EINVAL)
        return 4;


    int fd = api->open("/bin/sh", CB_O_WRONLY, 0);
    if (fd < 0)
        return 5;
    if (api->write(fd, "test", 4) >= 0)
        return 6;
    if (api->get_errno() != CB_EPERM)
        return 61;

    if (api->stat("/bin/sh", &statbuf) != 0)
        return 62;
    if (statbuf.type != CB_NODE_EXECUTABLE || statbuf.size != 0)
        return 63;

    if (api->spawn("/missing/sh", (char *[]){"sh", NULL}, NULL, NULL, 0, &child) == 0)
        return 7;
    if (api->get_errno() != CB_ENOENT)
        return 8;

    char long_name[4096];
    for (int i = 0; i < 4095; i++) long_name[i] = 'a';
    long_name[4095] = '\0';
    memcpy(long_name, "sh", 2); // Starts with "sh"

    if (api->spawn(long_name, (char *[]){"sh", NULL}, NULL, NULL, 0, &child) == 0)
        return 9;
    if (api->get_errno() != CB_ENAMETOOLONG)
        return 10;

    return 0;
}

const struct cb_program_v1 cb_vfs_executable_probe_program = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "vfsexecprobe", 0, 64 * 1024, vfs_executable_probe_main
};


