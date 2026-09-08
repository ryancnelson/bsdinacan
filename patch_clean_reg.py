import re

with open("src/core.c", "r") as f:
    text = f.read()

clean_func = """int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    char path[CB_PATH_MAX];
    int result;
    
    if (kernel == NULL || cb_executor_prepare(kernel, executor, source, &program) < 0) {
        return -1;
    }
        
    if (strchr(program->name, '/') != NULL) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    snprintf(path, sizeof(path), "/bin/%s", program->name);
    result = cb_vfs_create_executable(kernel, path, program);
    if (result < 0) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    return 0;
}
"""

text = re.sub(r'#include <stdio\.h>\nint cb_kernel_register_executor.*?\n}\n', clean_func, text, flags=re.DOTALL)
with open("src/core.c", "w") as f:
    f.write(text)
