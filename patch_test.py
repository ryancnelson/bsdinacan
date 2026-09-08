import re

with open("tests/test_core.c", "r") as f:
    text = f.read()

red_test = """
static int test_vfs_executable_main(const struct cb_api_v1 *api, int argc,
                                    char *const argv[], char *const envp[])
{
    struct cb_stat_v1 statbuf;
    cb_pid_t child;
    (void)argc; (void)argv; (void)envp;

    if (api->stat("/bin/sh", &statbuf) == 0)
        return 1;
    if (api->get_errno() != CB_ENOENT)
        return 2;

    if (api->spawn("/missing/sh", (char *[]){"sh", NULL}, NULL, NULL, 0, &child) == 0)
        return 3;
    if (api->get_errno() != CB_ENOENT)
        return 4;

    return 0;
}

static const struct cb_program_v1 test_vfs_executable_prog = {
    CB_ABI_VERSION_V1, sizeof(struct cb_program_v1), "test_vfs_exec", 0, 64 * 1024, test_vfs_executable_main
};

static void test_vfs_executable_nodes(void)
{
    struct cb_kernel *kernel = cb_kernel_create(cb_linux_host_ops());
    struct cb_task *task;
    int status;
    cb_kernel_register(kernel, &test_vfs_executable_prog);
    cb_kernel_register(kernel, cb_shell_program());
    
    task = task_create(kernel, NULL, program_find(kernel, "test_vfs_exec"), (char *[]){"test_vfs_exec", NULL}, NULL, NULL, 0);
    kernel->boot_pid = task->pid;
    cb_kernel_run(kernel);
    if (api_waitpid(task->pid, &status) != task->pid || status != 0)
        fail("test_vfs_executable_nodes failed");
    cb_kernel_destroy(kernel);
}
"""

text = text.replace("static void test_vfs_mount_routing(void)", red_test + "\nstatic void test_vfs_mount_routing(void)")
text = text.replace("    test_vfs_mount_routing();\n", "    test_vfs_executable_nodes();\n    test_vfs_mount_routing();\n")

with open("tests/test_core.c", "w") as f:
    f.write(text)
