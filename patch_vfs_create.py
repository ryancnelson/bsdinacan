import re

with open("src/vfs.c", "r") as f:
    text = f.read()

new_func = """
int cb_vfs_create_executable(struct cb_kernel *kernel, const char *path, struct cb_program *program)
{
    char normalized[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_vfs_node *parent;
    const char *name;
    struct cb_task dummy_task;
    int result;

    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.kernel = kernel;
    dummy_task.cwd = kernel->vfs_root;

    result = normalize_for_task(&dummy_task, path, normalized);
    if (result < 0) return result;

    result = resolve_normalized(&dummy_task, normalized, &node);
    if (result == 0) return -CB_EEXIST;
    if (result != -CB_ENOENT) return result;

    result = resolve_parent(&dummy_task, normalized, &parent, &name);
    if (result < 0) return result;

    result = parent->ops->create(parent, name, CB_NODE_EXECUTABLE, 0555, &node);
    if (result < 0) return result;

    node->object.executable = program;
    return 0;
}
"""
text += new_func

with open("src/vfs.c", "w") as f:
    f.write(text)

with open("src/internal.h", "r") as f:
    htext = f.read()
htext = htext.replace("int cb_vfs_mount_path(", "int cb_vfs_create_executable(struct cb_kernel *kernel, const char *path, struct cb_program *program);\nint cb_vfs_mount_path(")
with open("src/internal.h", "w") as f:
    f.write(htext)

with open("src/core.c", "r") as f:
    ctext = f.read()

old_register = """int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    struct cb_vfs_node *parent;
    struct cb_vfs_node *node;
    struct cb_task dummy_task;
    int result;
    
    if (kernel == NULL || cb_executor_prepare(kernel, executor, source, &program) < 0)
        return -1;
        
    if (strchr(program->name, '/') != NULL) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.kernel = kernel;
    dummy_task.cwd = kernel->vfs_root;
    
    result = resolve_normalized(&dummy_task, "/bin", &parent);
    if (result < 0) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    result = parent->ops->create(parent, program->name, CB_NODE_EXECUTABLE, 0555, &node);
    if (result < 0) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    node->object.executable = program;
    return 0;
}"""

new_register = """int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    char path[CB_PATH_MAX];
    
    if (kernel == NULL || cb_executor_prepare(kernel, executor, source, &program) < 0)
        return -1;
        
    if (strchr(program->name, '/') != NULL) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    snprintf(path, sizeof(path), "/bin/%s", program->name);
    if (cb_vfs_create_executable(kernel, path, program) < 0) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    
    return 0;
}"""
ctext = ctext.replace(old_register, new_register)
with open("src/core.c", "w") as f:
    f.write(ctext)
