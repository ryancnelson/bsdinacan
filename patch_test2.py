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
    int status;
    cb_kernel_register(kernel, &test_vfs_executable_prog);
    cb_kernel_register(kernel, &cb_shell_program);
    
    cb_kernel_boot(kernel, "test_vfs_exec");
    cb_kernel_run(kernel);
    if (kernel->boot_pid == 0)
        fail("boot failed");
    cb_kernel_destroy(kernel);
}
"""

text = re.sub(r'static int test_vfs_executable_main.*?\n}\n', red_test, text, flags=re.DOTALL)
text = text.replace("&cb_shell_program);", "&cb_shell_program); // fix")

with open("tests/test_core.c", "w") as f:
    f.write(text)
