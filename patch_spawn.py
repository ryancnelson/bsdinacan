import re

with open("src/core.c", "r") as f:
    text = f.read()

helper = """
static int resolve_executable_node(struct cb_task *task, const char *program_name, struct cb_vfs_node **node_out)
{
    char path[CB_PATH_MAX];
    struct cb_vfs_node *node;
    struct cb_stat_v1 statbuf;
    int result;
    
    if (strchr(program_name, '/') == NULL) {
        snprintf(path, sizeof(path), "/bin/%s", program_name);
    } else {
        snprintf(path, sizeof(path), "%s", program_name);
    }
    
    result = cb_vfs_lookup_node(task, path, &node);
    if (result < 0) return result;
    
    result = node->ops->stat(node, &statbuf);
    if (result < 0) return result;
    
    if (statbuf.type == CB_NODE_DIRECTORY || statbuf.type == CB_NODE_TERMINAL)
        return -CB_EACCES;
    if (statbuf.type != CB_NODE_EXECUTABLE)
        return -CB_ENOEXEC;
        
    *node_out = node;
    return 0;
}
"""

text = text.replace("static int api_spawn(", helper + "\nstatic int api_spawn(")

# api_spawn
old_spawn = """    struct cb_kernel *kernel = active_kernel;
    struct cb_task *parent = kernel->current;
    struct cb_program *program;
    struct cb_task *child;
    if (program_name == NULL || argv == NULL || argv[0] == NULL ||
        (action_count != 0 && actions == NULL)) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    if (string_vector_count(argv) > INT_MAX) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    program = program_find(kernel, program_name);
    if (program == NULL) {
        cb_task_set_error(parent, CB_ENOENT);
        return -1;
    }
    child = task_create(kernel, parent, program, argv, envp, actions,
                        action_count);
    if (child == NULL) {
        cb_task_set_error(parent, CB_ENOMEM);
        return -1;
    }
    if (pid_out != NULL)
        *pid_out = child->pid;
    cb_task_set_error(parent, 0);
    return 0;"""

new_spawn = """    struct cb_kernel *kernel = active_kernel;
    struct cb_task *parent = kernel->current;
    struct cb_vfs_node *node;
    struct cb_task *child;
    int result;
    if (program_name == NULL || argv == NULL || argv[0] == NULL ||
        (action_count != 0 && actions == NULL)) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    if (string_vector_count(argv) > INT_MAX) {
        cb_task_set_error(parent, CB_EINVAL);
        return -1;
    }
    
    result = resolve_executable_node(parent, program_name, &node);
    if (result < 0) {
        cb_task_set_error(parent, -result);
        return -1;
    }
    
    child = task_create(kernel, parent, node->object.executable, argv, envp, actions, action_count);
    if (child == NULL) {
        cb_task_set_error(parent, CB_ENOMEM);
        return -1;
    }
    
    // task_create successful, retain node for the child
    cb_vfs_node_retain(node);
    child->executable_node = node;
    
    if (pid_out != NULL)
        *pid_out = child->pid;
    cb_task_set_error(parent, 0);
    return 0;"""
text = text.replace(old_spawn, new_spawn)

# api_exec
old_exec = """    struct cb_task *task = active_kernel->current;
    struct cb_program *program;
    char **new_argv;
    char **new_environment;
    size_t argument_count;
    if (program_name == NULL || argv == NULL || argv[0] == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    argument_count = string_vector_count(argv);
    if (argument_count == SIZE_MAX || argument_count > INT_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    program = program_find(task->kernel, program_name);
    if (program == NULL) {
        cb_task_set_error(task, CB_ENOENT);
        return -1;
    }
    new_argv = string_vector_copy(task->kernel, argv);
    new_environment = string_vector_copy(task->kernel,
        envp != NULL ? envp : task->environment);
    if (new_argv == NULL || new_environment == NULL) {
        string_vector_destroy(task->kernel, new_argv);
        string_vector_destroy(task->kernel, new_environment);
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    task->pending_program = program;"""

new_exec = """    struct cb_task *task = active_kernel->current;
    struct cb_vfs_node *node;
    char **new_argv;
    char **new_environment;
    size_t argument_count;
    int result;
    if (program_name == NULL || argv == NULL || argv[0] == NULL) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    argument_count = string_vector_count(argv);
    if (argument_count == SIZE_MAX || argument_count > INT_MAX) {
        cb_task_set_error(task, CB_EINVAL);
        return -1;
    }
    
    result = resolve_executable_node(task, program_name, &node);
    if (result < 0) {
        cb_task_set_error(task, -result);
        return -1;
    }
    
    new_argv = string_vector_copy(task->kernel, argv);
    new_environment = string_vector_copy(task->kernel,
        envp != NULL ? envp : task->environment);
    if (new_argv == NULL || new_environment == NULL) {
        string_vector_destroy(task->kernel, new_argv);
        string_vector_destroy(task->kernel, new_environment);
        cb_task_set_error(task, CB_ENOMEM);
        return -1;
    }
    
    cb_vfs_node_retain(node);
    task->pending_executable_node = node;
    task->pending_program = node->object.executable;"""
text = text.replace(old_exec, new_exec)

# cb_kernel_boot
old_boot = """int cb_kernel_boot(struct cb_kernel *kernel, const char *command)
{
    struct cb_program *shell = program_find(kernel, "sh");
    struct cb_task *task;"""

new_boot = """int cb_kernel_boot(struct cb_kernel *kernel, const char *command)
{
    struct cb_vfs_node *shell_node;
    struct cb_task dummy_task;
    struct cb_task *task;
    int result;
    
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.kernel = kernel;
    dummy_task.cwd = kernel->vfs_root;
    
    result = resolve_executable_node(&dummy_task, "sh", &shell_node);
    if (result < 0) return -1;
    """
text = text.replace(old_boot, new_boot)

text = text.replace("task = task_create(kernel, NULL, shell,", "task = task_create(kernel, NULL, shell_node->object.executable,")
text = text.replace("if (kernel == NULL || shell == NULL || kernel->boot_pid != 0)", "if (kernel == NULL || kernel->boot_pid != 0)")
text = text.replace("""    kernel->boot_pid = task->pid;
    return 0;
}""", """    cb_vfs_node_retain(shell_node);
    task->executable_node = shell_node;
    kernel->boot_pid = task->pid;
    return 0;
}""")

with open("src/core.c", "w") as f:
    f.write(text)
