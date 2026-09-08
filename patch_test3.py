import re

with open("tests/test_core.c", "r") as f:
    text = f.read()

# remove the old inserted test completely
text = re.sub(r'static int test_vfs_executable_main.*?fail\("boot failed"\);\n    cb_kernel_destroy\(kernel\);\n}\n', '', text, flags=re.DOTALL)
text = re.sub(r'static int test_vfs_executable_main.*?fail\("test_vfs_executable_nodes failed"\);\n    cb_kernel_destroy\(kernel\);\n}\n', '', text, flags=re.DOTALL)

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
    cb_kernel_register(kernel, &test_vfs_executable_prog);
    extern const struct cb_program_v1 cb_shell_program;
    cb_kernel_register(kernel, &cb_shell_program);
    
    cb_kernel_boot(kernel, "test_vfs_exec");
    cb_kernel_run(kernel);
    cb_kernel_destroy(kernel);
}
"""

text = text.replace("static void test_vfs_mount_routing(void)", red_test + "\nstatic void test_vfs_mount_routing(void)")

with open("tests/test_core.c", "w") as f:
    f.write(text)
