import re

with open("src/core.c", "r") as f:
    text = f.read()

debug_func = """#include <stdio.h>
int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    char path[CB_PATH_MAX];
    int result;
    
    if (kernel == NULL || cb_executor_prepare(kernel, executor, source, &program) < 0) {
        printf("prepare failed\\n");
        return -1;
    }
        
    if (strchr(program->name, '/') != NULL) {
        printf("slash found\\n");
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    snprintf(path, sizeof(path), "/bin/%s", program->name);
    result = cb_vfs_create_executable(kernel, path, program);
    if (result < 0) {
        printf("create_executable failed %d for %s\\n", result, program->name);
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    return 0;
}
"""

text = re.sub(r'int cb_kernel_register_executor.*?\n}\n', debug_func, text, flags=re.DOTALL)
with open("src/core.c", "w") as f:
    f.write(text)
