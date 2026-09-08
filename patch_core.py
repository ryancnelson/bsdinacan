import re

with open("src/core.c", "r") as f:
    text = f.read()

# Replace program_find and cb_kernel_register_executor
old_reg = """static struct cb_program *program_find(struct cb_kernel *kernel,
                                       const char *name)
{
    size_t index;
    const char *base = strrchr(name, '/');
    if (base != NULL)
        name = base + 1;
    for (index = 0; index < kernel->program_count; ++index) {
        if (strcmp(kernel->programs[index]->name, name) == 0)
            return kernel->programs[index];
    }
    return NULL;
}"""
new_reg = """// program_find removed"""
text = text.replace(old_reg, new_reg)

old_register = """int cb_kernel_register_executor(struct cb_kernel *kernel,
                                const struct cb_executor_ops *executor,
                                const void *source)
{
    struct cb_program *program;
    if (kernel == NULL || kernel->program_count >= CB_MAX_PROGRAMS ||
        cb_executor_prepare(kernel, executor, source, &program) < 0)
        return -1;
    if (program_find(kernel, program->name) != NULL) {
        cb_executor_program_destroy(kernel, program);
        return -1;
    }
    kernel->programs[kernel->program_count++] = program;
    return 0;
}"""
new_register = """int cb_kernel_register_executor(struct cb_kernel *kernel,
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
text = text.replace(old_register, new_register)

with open("src/core.c", "w") as f:
    f.write(text)
